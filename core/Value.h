#ifndef VALUE_H
#define VALUE_H

#include <QtGlobal>
#include <QCoreApplication>
#include <QVariant>
#include <QMetaType>

class DiagnosticEmitterBase;

enum class ValueType{
    Void,       //!< runtime only; not for IR
    NodePtr,    //!< runtime only; not for IR; node index in IRRootInstance stored as int
    ValuePtr,   //!< runtime only; not for IR
    String,
    Int64
};

struct PtrCommon{
    int functionIndex;      //!< function index when the pointer is created
    int stmtIndex;          //!< statement index that creates this pointer
    int activationIndex;    //!< function activation index when the pointer is created
                            //!< for detection of dangling pointer to local variable, as well as debug
};

struct NodePtrType{
    PtrCommon head; //!< pointer common head
    int nodeIndex;  //!< node index in IRRootInstance
};
Q_DECLARE_METATYPE(NodePtrType);

struct ValuePtrType{
    // name resolution order: local variable, writable node member, read-only node member(node parameter), global variables
    enum PtrType{
        LocalVariable,
        NodeRWMember,
        NodeROParameter,
        GlobalVariable,
        NullPointer
    };

    PtrCommon head; //!< pointer common head
    PtrType ty;     //!< type of this pointer
    int valueIndex; //!< index of value being pointed
    int nodeIndex;  //!< only used for node variable
};
Q_DECLARE_METATYPE(ValuePtrType);

inline QMetaType::Type getQMetaType(ValueType ty){
    switch(ty){
    case ValueType::Void:       return QMetaType::UnknownType;
    case ValueType::NodePtr:    return static_cast<QMetaType::Type>(qMetaTypeId<NodePtrType>());
    case ValueType::ValuePtr:   return static_cast<QMetaType::Type>(qMetaTypeId<ValuePtrType>());
    case ValueType::String:     return QMetaType::QString;
    case ValueType::Int64:      return QMetaType::LongLong;
    }
    Q_UNREACHABLE();
}

inline ValueType getValueType(QMetaType::Type ty){
    switch(ty){
    case QMetaType::UnknownType:       return ValueType::Void;
    case QMetaType::QString:           return ValueType::String;
    case QMetaType::LongLong:          return ValueType::Int64;
    default:
        if(ty == static_cast<QMetaType::Type>(qMetaTypeId<NodePtrType>()))
            return ValueType::NodePtr;
        if(ty == static_cast<QMetaType::Type>(qMetaTypeId<ValuePtrType>()))
            return ValueType::ValuePtr;
    }
    Q_UNREACHABLE();
}

// we only use int and string as key
inline uint qHash(const QVariant& key, uint seed){
    switch(static_cast<QMetaType::Type>(key.userType())){
    case QMetaType::UnknownType:       return 0;
    case QMetaType::QString:           return qHash(key.toString(), seed);
    case QMetaType::LongLong:          return qHash(key.toLongLong(), seed);
    default: Q_UNREACHABLE();
    }
}

inline bool isValidIRValueType(ValueType ty){
    switch(ty){
    default:
        return false;
    case ValueType::String:
    case ValueType::Int64:
        return true;
    }
}

inline QString getTypeNameString(ValueType ty){
    switch(ty){
    case ValueType::Void:       return QCoreApplication::tr("Void",     "Value type name");
    case ValueType::NodePtr:    return QCoreApplication::tr("NodePtr",  "Value type name");
    case ValueType::ValuePtr:   return QCoreApplication::tr("ValuePtr", "Value type name");
    case ValueType::String:     return QCoreApplication::tr("String",   "Value type name");
    case ValueType::Int64:      return QCoreApplication::tr("Int64",    "Value type name");
    }
    Q_UNREACHABLE();
}

#endif // VALUE_H
