#include "AGType.h"

#include <CoreFoundation/CFString.h>

#include "ContextDescriptor.h"
#include "Metadata.h"
#include "MetadataVisitor.h"

CFStringRef AGTypeDescription(AGTypeID typeID) {
    auto type = reinterpret_cast<const AG::swift::metadata *>(typeID);

    CFMutableStringRef description = CFStringCreateMutable(kCFAllocatorDefault, 0);
    type->append_description(description);
    CFAutorelease(description);
    return description;
}

AGTypeKind AGTypeGetKind(AGTypeID typeID) {
    auto type = reinterpret_cast<const AG::swift::metadata *>(typeID);
    switch (type->getKind()) {
    case swift::MetadataKind::Class:
        return AGTypeKindClass;
    case swift::MetadataKind::Struct:
        return AGTypeKindStruct;
    case swift::MetadataKind::Enum:
        return AGTypeKindEnum;
    case swift::MetadataKind::Optional:
        return AGTypeKindOptional;
    case swift::MetadataKind::Tuple:
        return AGTypeKindTuple;
    case swift::MetadataKind::Function:
        return AGTypeKindFunction;
    case swift::MetadataKind::Existential:
        return AGTypeKindExistential;
    case swift::MetadataKind::Metatype:
        return AGTypeKindMetatype;
    default:
        return AGTypeKindNone;
    }
}

const AGTypeSignature AGTypeGetSignature(AGTypeID typeID) {
    auto type = reinterpret_cast<const AG::swift::metadata *>(typeID);
    const uint32_t *data = static_cast<const uint32_t *>(type->signature());
    if (!data) {
        return AGTypeSignature();
    }
    AGTypeSignature signature = {};
    signature.data[0] = data[0];
    signature.data[1] = data[1];
    signature.data[2] = data[2];
    signature.data[3] = data[3];
    signature.data[4] = data[4];
    return signature;
}

const void *AGTypeGetDescriptor(AGTypeID typeID) {
    auto type = reinterpret_cast<const AG::swift::metadata *>(typeID);
    return type->descriptor();
}

const void *AGTypeNominalDescriptor(AGTypeID typeID) {
    auto type = reinterpret_cast<const AG::swift::metadata *>(typeID);
    return type->nominal_descriptor();
}

const char *AGTypeNominalDescriptorName(AGTypeID typeID) {
    auto type = reinterpret_cast<const AG::swift::metadata *>(typeID);
    auto nominal_descriptor = type->nominal_descriptor();
    if (!nominal_descriptor) {
        return nullptr;
    }
    return nominal_descriptor->name();
}

void AGTypeApplyFields(AGTypeID typeID,
                       void (*body)(const char *field_name, size_t field_size, AGTypeID field_type, void *context),
                       void *context) {
    class Visitor : public AG::swift::metadata_visitor {
      private:
        void (*_body)(const char *field_name, size_t field_size, AGTypeID field_type, void *context);
        void *_context;

      public:
        Visitor(void (*body)(const char *field_name, size_t field_size, AGTypeID field_type, void *context),
                void *context)
            : _body(body), _context(context) {}

        bool unknown_result() const override { return true; }
        bool visit_field(const AG::swift::metadata &type, const AG::swift::field_record &field, size_t field_offset,
                         size_t field_size) const override {
            auto mangled_name = field.MangledTypeName.get();
            auto field_type = type.mangled_type_name_ref(mangled_name, true, nullptr);
            if (field_type) {
                auto field_name = field.FieldName.get();
                _body(field_name, field_offset, AGTypeID(field_type), _context);
            }
            return true;
        }
    };

    Visitor visitor = Visitor(body, context);

    auto type = reinterpret_cast<const AG::swift::metadata *>(typeID);
    type->visit(visitor);
}

bool AGTypeApplyFields2(AGTypeID typeID, AGTypeApplyOptions options,
                        bool (*body)(const char *field_name, size_t field_size, AGTypeID field_type, void *context),
                        void *context) {
    class Visitor : public AG::swift::metadata_visitor {
      private:
        AGTypeApplyOptions _options;
        bool (*_body)(const char *field_name, size_t field_size, AGTypeID field_type, void *context);
        void *_context;

      public:
        Visitor(AGTypeApplyOptions options,
                bool (*body)(const char *field_name, size_t field_size, AGTypeID field_type, void *context),
                void *context)
            : _options(options), _body(body), _context(context) {}

        bool unknown_result() const override { return _options & 2; }
        bool visit_field(const AG::swift::metadata &type, const AG::swift::field_record &field, size_t field_offset,
                         size_t field_size) const override {
            auto mangled_name = field.MangledTypeName.get();
            auto field_type = type.mangled_type_name_ref(mangled_name, true, nullptr);
            if (!field_type) {
                return unknown_result();
            }
            auto field_name = field.FieldName.get();
            _body(field_name, field_offset, AGTypeID(field_type), _context);
            return field_name != nullptr;
        }
        bool visit_case(const AG::swift::metadata &type, const AG::swift::field_record &field,
                        uint32_t index) const override {
            auto mangled_name = field.MangledTypeName.get();
            auto field_type = type.mangled_type_name_ref(mangled_name, true, nullptr);
            if (!field_type) {
                return unknown_result();
            }
            auto field_name = field.FieldName.get();
            _body(field_name, index, AGTypeID(field_type), _context);
            return field_name != nullptr;
        }
    };

    Visitor visitor = Visitor(options, body, context);

    auto type = reinterpret_cast<const AG::swift::metadata *>(typeID);
    switch (type->getKind()) {
    case ::swift::MetadataKind::Class:
        if (options & AGTypeApplyOptionsHeapClasses) {
            return type->visit_heap(visitor, AG::swift::metadata::visit_options::heap_class);
        }
        return false;
    case ::swift::MetadataKind::Struct:
        if (!(options & AGTypeApplyOptionsHeapClasses) && !(options & AGTypeApplyOptionsEnumCases)) {
            return type->visit(visitor);
        }
        return false;
    case ::swift::MetadataKind::Enum:
    case ::swift::MetadataKind::Optional:
        if (options & AGTypeApplyOptionsEnumCases) {
            return type->visit(visitor);
        }
        return false;
    case ::swift::MetadataKind::Tuple:
        if (!(options & AGTypeApplyOptionsHeapClasses) && !(options & AGTypeApplyOptionsEnumCases)) {
            return type->visit(visitor);
        }
        return false;
    default:
        return false;
    }
}
