# Copyright (c) 2013 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

"""Generates C++ source files from a mojom.Module."""

import datetime
import mojom
import mojom_pack
import os
import sys

from string import Template

# mojom_cpp_generator provides a way to generate c++ code from a mojom.Module.
# cpp = mojom_cpp_generator.CPPGenerator(module)
# cpp.GenerateFiles("/tmp/g")
def ReadTemplate(filename):
  """Reads a string.Template from |filename|."""
  dir = os.path.dirname(__file__)
  with open(dir + '/' + filename, 'r') as file:
    return Template(file.read())


def AddForward(forwards, kind):
  """Adds a forward class declaration line to the |forwards| set."""
  if isinstance(kind, mojom.Struct):
    forwards.add("class %s;" % kind.name.capitalize())
  if isinstance(kind, mojom.Array):
    AddForward(forwards, kind.kind)


def GetForwards(forwards):
  """Returns a string suitable for substituting into a $FORWARDS section."""
  if len(forwards) > 0:
    return '\n'.join(sorted(forwards)) + '\n'
  return ""


class CPPGenerator(object):

  struct_header_template = ReadTemplate("module_struct.h")
  struct_source_template = ReadTemplate("module_struct.cc")
  struct_serialization_header_template = \
      ReadTemplate("module_struct_serialization.h")
  struct_serialization_source_template = \
      ReadTemplate("module_struct_serialization.cc")
  struct_serialization_compute_template = \
    Template(" +\n      mojo::internal::ComputeSizeOf($NAME->$FIELD())")
  struct_serialization_clone_template = Template(
      "  clone->set_$FIELD(mojo::internal::Clone($NAME->$FIELD(), buf));")
  struct_serialization_encode_template = Template(
      "  Encode(&$NAME->${FIELD}_, handles);")
  struct_serialization_decode_template = Template(
      "  if (!Decode(&$NAME->${FIELD}_, message))\n    return false;")
  interface_header_template = ReadTemplate("module_interface.h")
  interface_stub_header_template = ReadTemplate("module_interface_stub.h")
  interface_stub_source_template = ReadTemplate("module_interface_stub.cc")
  interface_stub_case_template = ReadTemplate("module_interface_stub_case")
  field_template = Template("  $itype ${field}_;")
  bool_field_template = Template("  uint8_t ${field}_ : 1;")
  setter_template = \
      Template("  void set_$field($stype $field) { ${field}_ = $field; }")
  getter_template = \
      Template("  $gtype $field() const { return ${field}_; }")
  ptr_getter_template = \
      Template("  $gtype $field() const { return ${field}_.ptr; }")
  pad_template = Template("  uint8_t _pad${count}_[$pad];")
  HEADER_SIZE = 8

  kind_to_type = {
    mojom.BOOL:   "bool",
    mojom.INT8:   "int8_t",
    mojom.UINT8:  "uint8_t",
    mojom.INT16:  "int16_t",
    mojom.UINT16: "uint16_t",
    mojom.INT32:  "int32_t",
    mojom.UINT32: "uint32_t",
    mojom.FLOAT:  "float",
    mojom.HANDLE: "mojo::Handle",
    mojom.INT64:  "int64_t",
    mojom.UINT64: "uint64_t",
    mojom.DOUBLE: "double",
  }

  @classmethod
  def GetType(cls, kind):
    if isinstance(kind, mojom.Struct):
      return "%s*" % kind.name.capitalize()
    if isinstance(kind, mojom.Array):
      return "mojo::Array<%s>*" % cls.GetType(kind.kind)
    if kind.spec == 's':
      return "mojo::String*"
    return cls.kind_to_type[kind]

  @classmethod
  def GetConstType(cls, kind):
    if isinstance(kind, mojom.Struct):
      return "const %s*" % kind.name.capitalize()
    if isinstance(kind, mojom.Array):
      return "const mojo::Array<%s>*" % cls.GetConstType(kind.kind)
    if kind.spec == 's':
      return "const mojo::String*"
    return cls.kind_to_type[kind]

  @classmethod
  def GetGetterLine(cls, pf):
    kind = pf.field.kind
    template = None
    gtype = cls.GetConstType(kind)
    if isinstance(kind, (mojom.Struct, mojom.Array)) or kind.spec == 's':
      template = cls.ptr_getter_template
    else:
      template = cls.getter_template
    return template.substitute(field=pf.field.name, gtype=gtype)

  @classmethod
  def GetSetterLine(cls, pf):
    stype = cls.GetType(pf.field.kind)
    return cls.setter_template.substitute(field=pf.field.name, stype=stype)

  @classmethod
  def GetFieldLine(cls, pf):
    kind = pf.field.kind
    if kind.spec == 'b':
      return cls.bool_field_template.substitute(field=pf.field.name)
    itype = None
    if isinstance(kind, mojom.Struct):
      itype = "mojo::internal::StructPointer<%s>" % kind.name.capitalize()
    elif isinstance(kind, mojom.Array):
      itype = "mojo::internal::StructPointer<%s>" % cls.GetType(kind.kind)
    elif kind.spec == 's':
      itype = "mojo::internal::StringPointer"
    else:
      itype = cls.kind_to_type[kind]
    return cls.field_template.substitute(field=pf.field.name, itype=itype)

  @classmethod
  def GetCaseLine(cls, interface, method):
    method_call = "%s(" % method.name
    first_param = True
    for param in method.parameters:
      if first_param:
        first_param = False
      else:
        method_call += ", "
      method_call += "params->%s()" % param.name
    method_call += ");"
    return cls.interface_stub_case_template.substitute(
        CLASS = interface.name,
        METHOD = method.name,
        METHOD_CALL = method_call);

  @classmethod
  def GetSerializedFields(cls, ps):
    fields = []
    for pf in ps.packed_fields:
      if isinstance(pf.field.kind, (mojom.Struct, mojom.Array)) or \
          pf.field.kind.spec == 's':
        fields.append(pf.field)
    return fields

  def GetHeaderGuard(self, name):
    return "MOJO_GENERATED_BINDINGS_%s_%s_H_" % \
        (self.module.name.upper(), name.upper())

  def GetHeaderFile(self, *components):
    component_string = '_'.join(components)
    return os.path.join(
      self.header_dir,
      "%s_%s.h" % (self.module.name.lower(), component_string.lower()))

  # Pass |output_dir| to emit files to disk. Omit |output_dir| to echo all files
  # to stdout.
  def __init__(self, module, header_dir, output_dir=None):
    self.module = module
    self.header_dir = header_dir
    self.output_dir = output_dir

  def WriteTemplateToFile(self, template, name, ext, **substitutions):
    if self.output_dir is None:
      file = sys.stdout
    else:
      filename = "%s_%s.%s" % \
          (self.module.name.lower(), name.lower(), ext)
      file = open(os.path.join(self.output_dir, filename), "w+")
    try:
      file.write(template.substitute(substitutions))
    finally:
      if self.output_dir is not None:
        file.close()

  def GenerateStructHeader(self, ps):
    struct = ps.struct
    fields = []
    setters = []
    getters = []
    forwards = set()

    pad_count = 0
    num_fields = len(ps.packed_fields)
    for i in xrange(num_fields):
      pf = ps.packed_fields[i]
      fields.append(self.GetFieldLine(pf))
      AddForward(forwards, pf.field.kind)
      if i < (num_fields - 2):
        next_pf = ps.packed_fields[i+1]
        pad = next_pf.offset - (pf.offset + pf.size)
        if pad > 0:
          fields.append(self.pad_template.substitute(count=pad_count, pad=pad))
          pad_count += 1
      setters.append(self.GetSetterLine(pf))
      getters.append(self.GetGetterLine(pf))

    if num_fields > 0:
      last_field = ps.packed_fields[num_fields - 1]
      offset = last_field.offset + last_field.size
      pad = mojom_pack.GetPad(offset, 8)
      if pad > 0:
        fields.append(self.pad_template.substitute(count=pad_count, pad=pad))
        pad_count += 1
      size = offset + pad
    else:
      size = 0

    self.WriteTemplateToFile(self.struct_header_template, struct.name, 'h',
        YEAR = datetime.date.today().year,
        HEADER_GUARD = self.GetHeaderGuard(struct.name),
        NAMESPACE = self.module.namespace,
        CLASS = struct.name.capitalize(),
        SIZE = size + self.HEADER_SIZE,
        FORWARDS = GetForwards(forwards),
        SETTERS = '\n'.join(setters),
        GETTERS = '\n'.join(getters),
        FIELDS = '\n'.join(fields))

  def GenerateStructSource(self, ps):
    struct = ps.struct
    header = self.GetHeaderFile(struct.name)
    self.WriteTemplateToFile(self.struct_source_template, struct.name, 'cc',
        YEAR = datetime.date.today().year,
        NAMESPACE = self.module.namespace,
        CLASS = struct.name.capitalize(),
        NUM_FIELDS = len(struct.fields),
        HEADER = header)

  def GenerateStructSerializationHeader(self, ps):
    struct = ps.struct
    header = self.GetHeaderFile(struct.name, "serialization")
    self.WriteTemplateToFile(
        self.struct_serialization_header_template,
        struct.name + "_serialization",
        'h',
        YEAR = datetime.date.today().year,
        HEADER_GUARD = self.GetHeaderGuard(struct.name + "_SERIALIZATION"),
        NAMESPACE = self.module.namespace,
        CLASS = struct.name.capitalize(),
        FULL_CLASS = "%s::%s" % \
            (self.module.namespace, struct.name.capitalize()),
        HEADER = header)

  def GenerateStructSerializationSource(self, ps):
    struct = ps.struct
    serialization_header = self.GetHeaderFile(struct.name, "serialization")
    class_header = self.GetHeaderFile(struct.name)
    clones = []
    encodes = []
    decodes = []
    sizes = "  return sizeof(*%s)" % struct.name.lower()
    fields = self.GetSerializedFields(ps)
    for field in fields:
      substitutions = {'NAME': struct.name.lower(), 'FIELD': field.name.lower()}
      sizes += \
          self.struct_serialization_compute_template.substitute(substitutions)
      clones.append(
          self.struct_serialization_clone_template.substitute(substitutions))
      encodes.append(
          self.struct_serialization_encode_template.substitute(substitutions))
      decodes.append(
          self.struct_serialization_decode_template.substitute(substitutions))
    sizes += ";"
    self.WriteTemplateToFile(
        self.struct_serialization_source_template,
        struct.name + "_serialization",
        'cc',
        YEAR = datetime.date.today().year,
        NAME = struct.name.lower(),
        FULL_CLASS = "%s::%s" % \
            (self.module.namespace, struct.name.capitalize()),
        SERIALIZATION_HEADER = serialization_header,
        CLASS_HEADER = class_header,
        SIZES = sizes,
        CLONES = '\n'.join(clones),
        ENCODES = '\n'.join(encodes),
        DECODES = '\n'.join(decodes))

  def GenerateInterfaceHeader(self, interface):
    cpp_methods = []
    forwards = set()
    for method in interface.methods:
      cpp_method = "  virtual void %s(" % method.name
      first_param = True
      for param in method.parameters:
        if first_param == True:
          first_param = False
        else:
          cpp_method += ", "
        AddForward(forwards, param.kind)
        cpp_method += "%s %s" % (self.GetConstType(param.kind), param.name)
      cpp_method += ") = 0;"
      cpp_methods.append(cpp_method)

    self.WriteTemplateToFile(
        self.interface_header_template,
        interface.name,
        'h',
        YEAR = datetime.date.today().year,
        HEADER_GUARD = self.GetHeaderGuard(interface.name),
        NAMESPACE = self.module.namespace,
        CLASS = interface.name.capitalize(),
        FORWARDS = GetForwards(forwards),
        METHODS = '\n'.join(cpp_methods))

  def GenerateInterfaceStubHeader(self, interface):
    header = self.GetHeaderFile(interface.name)
    self.WriteTemplateToFile(
        self.interface_stub_header_template,
        interface.name + "_stub",
        'h',
        YEAR = datetime.date.today().year,
        HEADER_GUARD = self.GetHeaderGuard(interface.name + "_STUB"),
        NAMESPACE = self.module.namespace,
        CLASS = interface.name.capitalize(),
        HEADER = header)

  def GenerateInterfaceStubSource(self, interface):
    stub_header = self.GetHeaderFile(interface.name, "stub")
    serialization_header = self.GetHeaderFile(interface.name, "serialization")
    cases = []
    for method in interface.methods:
      cases.append(self.GetCaseLine(interface, method))
    self.WriteTemplateToFile(
        self.interface_stub_source_template,
        interface.name + "_stub",
        'cc',
        YEAR = datetime.date.today().year,
        NAMESPACE = self.module.namespace,
        CLASS = interface.name.capitalize(),
        CASES = '\n'.join(cases),
        STUB_HEADER = stub_header,
        SERIALIZATION_HEADER = serialization_header)

  def GenerateFiles(self):
    for struct in self.module.structs:
      ps = mojom_pack.PackedStruct(struct)
      self.GenerateStructHeader(ps)
      self.GenerateStructSource(ps)
      self.GenerateStructSerializationHeader(ps)
      self.GenerateStructSerializationSource(ps)
    for interface in self.module.interfaces:
      self.GenerateInterfaceHeader(interface)
      self.GenerateInterfaceStubHeader(interface)
      self.GenerateInterfaceStubSource(interface)
