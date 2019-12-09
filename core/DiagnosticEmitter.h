#ifndef DIAGNOSTICEMITTER_H
#define DIAGNOSTICEMITTER_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantList>
#include <QMetaEnum>
#include <QCoreApplication>

#include "core/Value.h"

/**
 * @brief The Diag class lists all the possible diagnostics that the program can generate
 *
 * The description beside enum can be shown as tooltip in QtCreator
 */
class Diag : private QObject
{
    Q_OBJECT

public:
    enum ID{// [Diagnostic type] [Module] (Scenario]) [Major problem] ([Scenario / Cause]...)
        Warn_Exec_UninitializedRead,        //!< (no argument)
        Warn_Task_UnreachableFunction,      //!< [FunctionName]

        Error_BadName_EmptyString,                          //!< (no argument)
        Error_BadName_IllegalChar,                          //!< [CharAsString][NameString]
        Error_BadName_UnprintableChar,                      //!< (no argument)
        Error_BadName_PureNumber,                           //!< [NameString][NumberInt]

        Error_IR_BadType_BadTypeForNodeParam,               //!< [ParamName][ParamType]
        Error_IR_NameClash_NodeParam,                       //!< [ParamName][FirstParamIndex][SecondParamIndex]
        Error_IR_NameClash_NodeType,                        //!< [NodeTypeName]
        Error_IR_BadPrimaryKey_KeyNotFound,                 //!< [ParamName]
        Error_IR_BadPrimaryKey_KeyNotUnique,                //!< [ParamName]
        Error_IR_BadReference_ChildNodeType,                //!< [ChildNodeTypeName]
        Error_IR_BadReference_RootNodeType,                 //!< [RootNodeTypeName]
        Error_IR_DuplicatedReference_ChildNodeType,         //!< [ChildNodeTypeName]
        Error_IR_BadParameterList_Count,                    //!< [ExpectedParamCount][ProvidedParamCount]
        Error_IR_BadParameterList_Type,                     //!< [ParamIndex][ExpectedParamType][ProvidedParamCount]
        Error_IR_BadTree_UnexpectedChild,                   //!< [ChildNodeTypeName]
        Error_IR_BadTree_BrokenConstraint_ParamNotUnique,   //!< [ChildNodeTypeName][ParamName][FirstNodeIndex][SecondNodeIndex][ParamValueAsString]
        Error_IR_BadTree_EmptyTree,                         //!< (no argument)
        Error_IR_BadTree_DuplicatedReference_ChildNode,     //!< [NodeIndex][FirstParent][SecondParent]
        Error_IR_BadTree_BadNodeOrder,                      //!< [ChildNodeIndex][ParentNodeIndex]
        Error_IR_BadTree_ConflictingParentReference,        //!< [ChildNodeIndex][ParentIndexFromChild][ParentIndexFromTraversal]
        Error_IR_BadTree_BadNodeTypeIndex,                  //!< [NodeIndex][NodeTypeIndex]
        Error_IR_BadTree_UnreachableNode,                   //!< [NodeIndex]

        Error_Task_BadInitializer_ExternVariable,           //!< [VarName][VarType][InitializerType]
        Error_Task_NameClash_ExternVariable,                //!< [VarName][FirstDeclIndex][SecondDeclIndex]
        Error_Task_NameClash_Function,                      //!< [FunctionName][FirstIndex][SecondIndex]
        Error_Task_BadFunctionIndex_NodeTraverseCallback,   //!< [NodeTypeName][PassIndex][FunctionIndex]
        Error_Task_NoCallback,                              //!< (no argument)

        Error_Func_NameClash_ExternVariable,                //!< [VarName][FirstDeclIndex][SecondDeclIndex]
        Error_Func_NameClash_LocalVariable,                 //!< [VarName][FirstDeclIndex][SecondDeclIndex]
        Error_Func_BadType_ExternVariableVoid,              //!< [VarName]
        Error_Func_BadType_LocalVariableVoid,               //!< [VarName]
        Error_Func_InvalidValue_TotalParamCount,            //!< [TotalParamCount]
        Error_Func_InvalidValue_RequiredParamCount,         //!< [RequiredParamCount]
        Error_Func_MissingInitializer_OptionalParam,        //!< [ParamIndex][ParamName]
        Error_Func_BadInitializer_LocalVariable,            //!< [ParamIndex][ParamName][ParamType][InitializerType]
        Error_Func_BadExprDependence_BadIndex,              //!< [DependentExprIndex][DependedExprIndex]
        Error_Func_BadExprDependence_TypeMismatch,          //!< [DependentExprIndex][DependedExprIndex][ExpectedType][DependedExprType]
        Error_Func_BadExpr_BadNameReference,                //!< [ExprIndex][VarName]
        Error_Func_Stmt_BadExprIndex,                       //!< [ExprIndex]
        Error_Func_Stmt_BadExprIndex_BranchCondition,       //!< [ExprIndex][BranchCaseIndex]
        Error_Func_Assign_BadRHS_RHSVoid,                   //!< [ExprIndex]
        Error_Func_Assign_BadRHS_VariableTypeMismatch,      //!< [VarName][VarType][ExprIndex][ExprType]
        Error_Func_Assign_BadLHS_Type,                      //!< [ExprIndex][ExprType]
        Error_Func_Assign_BadLHS_BadNameReference,          //!< [VarName]
        Error_Func_Output_BadRHS_Type,                      //!< [ExprIndex][ExprType]
        Error_Func_Call_CalleeNotFound,                     //!< [FunctionName]
        Error_Func_Call_BadParamList_Count,                 //!< [FunctionName][TotalParamCount][RequiredParamCount][ProvidedArgumentCount]
        Error_Func_Call_BadParamList_Type,                  //!< [FunctionName][ParamIndex][ParamName][ParamType][ProvidedArgumentType]
        Error_Func_Branch_BadLabelReference,                //!< [LabelName][BranchCaseIndex]
        Error_Func_Branch_BadConditionType,                 //!< [BranchCaseIndex][BranchConditionExprIndex][BranchConditionExprType]
        Error_Func_DuplicateLabel,                          //!< [LabelName][FirstLabelAddress][SecondLabelAddress]

        Error_Exec_TypeMismatch_ReadByName,                 //!< [ExpectedTy][VarTy][VarName]
        Error_Exec_TypeMismatch_WriteByName,                //!< [ExpectedTy][VarTy][VarName]
        Error_Exec_TypeMismatch_WriteByPointer,             //!< [ExpectedTy][VarTy][PtrDescriptionString]
        Error_Exec_TypeMismatch_ExpressionDependency,       //!< [ExpectedTy][EvaluatedTy][DependentExprIndex][DependedExprIndex]
        Error_Exec_BadReference_VariableRead,               //!< [VarName]
        Error_Exec_BadReference_VariableWrite,              //!< [VarName]
        Error_Exec_BadReference_VariableTakeAddress,        //!< [VarName]
        Error_Exec_NullPointerException_ReadValue,          //!< [PtrDescriptionString]
        Error_Exec_NullPointerException_WriteValue,         //!< [PtrDescriptionString]
        Error_Exec_DanglingPointerException_ReadValue,      //!< [PtrDescriptionString]
        Error_Exec_DanglingPointerException_WriteValue,     //!< [PtrDescriptionString]
        Error_Exec_WriteToConst_WriteNodeParamByName,       //!< [VarName]
        Error_Exec_WriteToConst_WriteNodeParamByPointer,    //!< [PtrDescriptionString]
        Error_Exec_BadNodePointer_TraverseToParent,         //!< [PtrDescriptionString]
        Error_Exec_BadNodePointer_TraverseToChild,          //!< [PtrDescriptionString]
        Error_Exec_BadTraverse_ChildWithoutPrimaryKey,      //!< [ChildNodeTypeName][PtrDescriptionString]
        Error_Exec_BadTraverse_PrimaryKeyTypeMismatch,      //!< [ProvidedKeyTy][ActualKeyTy][ChildNodeTypeName][KeyName][PtrDescriptionString]
        Error_Exec_BadTraverse_ParameterNotFound,           //!< [ChildNodeTypeName][KeyName][PtrDescriptionString]
        Error_Exec_BadTraverse_ParameterNotUnique,          //!< [ChildNodeTypeName][KeyName][PtrDescriptionString]
        Error_Exec_BadTraverse_UniqueKeyTypeMismatch,       //!< [ProvidedKeyTy][ActualKeyTy][ChildNodeTypeName][KeyName][PtrDescriptionString]
        Error_Exec_Unreachable,                             //!< (no argument)
        Error_Exec_Assign_InvalidLHSType,                   //!< [ProvidedLHSType]
        Error_Exec_Output_Unknown_String,                   //!< [OutputString]
        Error_Exec_Output_InvalidType,                      //!< [ProvidedType]
        Error_Exec_Call_BadReference,                       //!< [FunctionName]
        Error_Exec_Call_BadArgumentList_Count,              //!< [FunctionName][RequiredParamCount][TotalParamCount][ProvidedArgumentCount]
        Error_Exec_Call_BadArgumentList_Type,               //!< [FunctionName][ParameterIndex][ParameterName][ParameterType][ProvidedArgumentType]
        Error_Exec_Branch_InvalidConditionType,             //!< [CaseIndex][ProvidedType]
        Error_Exec_Branch_InvalidLabelAddress,              //!< [CaseIndex][LabelMarkedStatementIndex]
        Error_Exec_Branch_Unreachable,                      //!< [CaseIndex]

        Error_Parser_NameClash_MatchPair,                       //!< [MatchPairName][FirstIndex][SecondIndex]
        Error_Parser_NameClash_ParserNode,                      //!< [NodeName][1stIndex][2ndIndex]
        Error_Parser_NameClash_ParserNodeParameter,             //!< [ParameterName][1stIndex][2ndIndex]
        Error_Parser_BadMatchPair_EmptyStartString,             //!< [StartStringIndex]
        Error_Parser_BadMatchPair_EmptyEndString,               //!< [EndStringIndex]
        Error_Parser_BadMatchPair_StartStringConflict,          //!< [StartString][1stMatchPairName][1stMatchPair1Index][2ndMatchPairName][2ndMatchPairIndex]
        Error_Parser_BadMatchPair_EndStringDuplicated,          //!< [MatchPairIndex][EndString][FirstIndex][SecondIndex]
        Error_Parser_BadMatchPair_NoStartString,                //!< (no argument)
        Error_Parser_BadMatchPair_NoEndString,                  //!< (no argument)
        Error_Parser_BadExprMatchPair_EmptyStartString,         //!< (no argument)
        Error_Parser_BadExprMatchPair_EmptyEndString,           //!< (no argument)
        Error_Parser_BadExprMatchPair_StartStringInIgnoreList,  //!< (no argument)
        Error_Parser_BadExprMatchPair_EndStringInIgnoreList,    //!< (no argument)
        Error_Parser_BadReference_IRNodeName,                   //!< [IRNodeName]
        Error_Parser_BadReference_ParserNodeName,               //!< [ParserNodeName]
        Error_Parser_MultipleOverwrite,                         //!< [ValueName][FirstIndex][SecondIndex]
        Error_Parser_BadConversionToIR_IRParamNotInitialized,   //!< [ParameterName]
        Error_Parser_BadConversionToIR_IRParamNotExist,         //!< [ParameterName]
        Error_Parser_BadRoot_BadReferenceByParserNodeName,      //!< [RootParserNodeName]
        Error_Parser_BadRoot_NotConvertingToIR,                 //!< [RootParserNodeName]
        Error_Parser_BadTree_BadChildNodeReference,             //!< [ParentParserNodeName][ChildParserNodeName]

        Error_Parser_BadPattern_Expr_MissingEngineNameEndMark,          //!< [StringDiagnostic]
        Error_Parser_BadPattern_Expr_NoRawLiteralAfterEngineSpecifier,  //!< [StringDiagnostic]
        Error_Parser_BadPattern_Expr_ExpectingExpressionContent,        //!< [StringDiagnostic]
        Error_Parser_BadPattern_Expr_UnterminatedQuote,                 //!< [StringDiagnostic]
        Error_Parser_BadPattern_Expr_RawStringMissingQuoteStart,        //!< [StringDiagnostic]
        Error_Parser_BadPattern_Expr_UnterminatedExpr,                  //!< [StringDiagnostic]
        Error_Parser_BadPattern_Expr_EmptyBody,                         //!< [StringDiagnostic]
        Error_Parser_BadPattern_Expr_GarbageAtEnd,                      //!< [StringDiagnostic]
        Error_Parser_BadPattern_Expr_UnrecognizedEngine,                //!< [StringDiagnostic]
        Error_Parser_BadPattern_Expr_BadRegex,                          //!< [StringDiagnostic][RegexErrorString]
        Error_Parser_BadPattern_Expr_DuplicatedDefinition,              //!< [ValueName][FirstSubpatternIndex][SecondSubpatternIndex]
        Error_Parser_BadPattern_Expr_BadTerminatorInclusionSpecifier,   //!< [StringDiagnostic]
        Error_Parser_BadPattern_Expr_BadNameForReference,               //!< [StringDiagnostic]
        Error_Parser_BadPattern_Expr_InvalidNextPatternForInclusion,    //!< [StringDiagnostic]
        Error_Parser_BadPattern_Expr_UnexpectedMatchPairEnd,            //!< [StringDiagnostic]
        Error_Parser_BadPattern_UnmatchedMatchPairStart,                //!< [StringDiagnostic]
        Error_Parser_BadPattern_EmptyPattern,                           //!< (no argument)

        Error_Parser_BadValueTransform_UnterminatedExpr,                //!< [StringDiagnostic]
        Error_Parser_BadValueTransform_NonLocalAccessInLocalOnlyEnv,    //!< [StringDiagnostic]
        Error_Parser_BadValueTransform_InvalidNameForReference,         //!< [StringDiagnostic]
        Error_Parser_BadValueTransform_MissingChildSearchExpr,          //!< [StringDiagnostic]
        Error_Parser_BadValueTransform_UnterminatedChildSearchExpr,     //!< [StringDiagnostic]
        Error_Parser_BadValueTransform_BadNumberExpr,                   //!< [StringDiagnostic]
        Error_Parser_BadValueTransform_ExpectingLiteralExpr,            //!< [StringDiagnostic]
        Error_Parser_BadValueTransform_UnterminatedQuote,               //!< [StringDiagnostic]
        Error_Parser_BadValueTransform_RawStringMissingQuoteStart,      //!< [StringDiagnostic]
        Error_Parser_BadValueTransform_GarbageAtExprEnd,                //!< [StringDiagnostic]
        Error_Parser_BadValueTransform_Traverse_ExpectSlashOrDot,       //!< [StringDiagnostic]
        Error_Parser_BadValueTransform_ExpectTraverseExpr,              //!< [StringDiagnostic]
        Error_Parser_BadValueTransform_ExpectValueName,                 //!< [StringDiagnostic]

        Warn_Parser_MissingInitializer,                     //!< [ParameterName]
        Warn_Parser_Unused_Overwrite,                       //!< [OverwriteValueName][OverwriteRecordIndex]
        Warn_Parser_Unused_PatternValue,                    //!< [PatternValueName][SubPatternIndex]
        Warn_Parser_DuplicatedReference_ChildParserNode,    //!< [ParentParserNodeName][ChildParserNodeName]
        Warn_Parser_UnreachableNode,                        //!< [ParserNodeName]

        Warn_Parser_Matching_Ambiguous,                     //!< [InputString], [list of tuple of [NodeName][NodeParamStringList][PatternIndex]]

        Error_Parser_Matching_NoMatch,                      //!< (no argument)
        Error_Parser_Matching_GarbageAtEnd,                 //!< (no argument)
        Error_Parser_IRBuild_BadTransform,                  //!< [ParserNodeName][IRNodeName][IRNodeParamName]
        Error_Parser_IRBuild_BadCast,                       //!< [ParserNodeName][IRNodeName][IRNodeParamName][IRNodeParameterType][ParserNodeDataString]

        Error_Json_UnknownType_String,      //!< [TypeString]
        Error_Json_UnsupportedLiteralType,  //!< (no argument)
        Error_Json_UnexpectedInitializer,   //!< [VarName][VarType]
        Error_Json_UnknownBranchAction,     //!< [ActString]
        Error_Json_UnknownStatementType,    //!< [StmtString]

        Error_Json_BadReference_Variable,   //!< [VarName]
        Error_Json_BadReference_IR,         //!< [IRName]
        Error_Json_BadReference_Output,     //!< [OutputName]
        Error_Json_BadReference_IRNodeType, //!< [IRNodeTypeName]

        Warn_XML_MismatchedIRTypeName,              //!< [LineNumber][ColumnNumber][ExpectedIRRootTypeName][ActualIRRootTypeName]
        Warn_XML_UnexpectedAttribute,               //!< [LineNumber][ColumnNumber][ElementName][AttributeName][AttributeValue]
        Warn_XML_IRNode_MissingParameter,           //!< [LineNumber][ColumnNumber]
        Error_XML_UnexpectedElement,                //!< [LineNumber][ColumnNumber][ExpectedElementName][ActualElementName]
        Error_XML_InvalidXML,                       //!< [LineNumber][ColumnNumber][ErrorString]
        Error_XML_ExpectingIRRootInstance,          //!< [LineNumber][ColumnNumber]
        Error_XML_UnknownIRNodeType,                //!< [LineNumber][ColumnNumber][IRNodeTypeName]
        Error_XML_ExpectingStartElement,            //!< [LineNumber][ColumnNumber][TokenString]
        Error_XML_IRNode_Param_MissingName,         //!< [LineNumber][ColumnNumber]
        Error_XML_IRNode_Param_MissingType,         //!< [LineNumber][ColumnNumber]
        Error_XML_IRNode_Param_UnknownParam,        //!< [LineNumber][ColumnNumber][ParameterName]
        Error_XML_UnknownValueType,                 //!< [LineNumber][ColumnNumber][ValueTypeName]
        Error_XML_IRNode_Param_MissingData,         //!< [LineNumber][ColumnNumber][ParameterName]
        Error_XML_IRNode_Param_ExpectEndElement,    //!< [LineNumber][ColumnNumber][ParameterName]
        Error_XML_IRNode_Param_TypeMismatch,        //!< [LineNumber][ColumnNumber][ParameterName][ExpectedType][ActualType]
        Error_XML_IRNode_Param_InvalidValue,        //!< [LineNumber][ColumnNumber][ParameterName][ParameterType][ValueString]
        Error_XML_IRNode_Param_MultipleValue,       //!< [LineNumber][ColumnNumber][ParameterName]
        Error_XML_IRNode_ParamAfterChildNode,       //!< [LineNumber][ColumnNumber]

        InvalidID
    };
    Q_ENUM(ID)

    static QString getString(ID id){
        return QMetaEnum::fromType<ID>().valueToKey(id);
    }

private:
    Diag(){} // you should not create instance

};

// we want to be able to put ValueType into QVariant for diagnostic only
struct ValueTypeWrapper{
    ValueType ty;
};
Q_DECLARE_METATYPE(ValueTypeWrapper)

/**
 * @brief The ExpressionDiagnosticRecord struct describes a diagnostic to an expression string
 *
 * Each diagnostic can carry an error interval and an info interval.
 * Info interval may and may not overlap with error interval.
 * If it makes no sense for an interval, both start and end index should be zero.
 */
struct StringDiagnosticRecord{
    QString str;
    int infoStart;
    int infoEnd;
    int errorStart;
    int errorEnd;
};
Q_DECLARE_METATYPE(StringDiagnosticRecord)

class DiagnosticPathNode
{
    friend class DiagnosticEmitterBase;
public:
    DiagnosticPathNode(DiagnosticEmitterBase& d, const QString& pathName);

    DiagnosticPathNode(const DiagnosticPathNode&) = delete;
    DiagnosticPathNode(DiagnosticPathNode&&) = delete;

    ~DiagnosticPathNode(){
        if(hierarchyIndex >= 0){
            release();
        }
    }
    void pop(){
        if(hierarchyIndex >= 0){
            release();
        }
    }

    void setDetailedName(const QString& name){
        detailedName = name;
    }

    const QString& getPathName()const {return pathName;}
    const QString& getDetailedName()const {return detailedName;}
    const DiagnosticPathNode* getPrev()const {return prev;}
    int getHierarchyIndex() const {return hierarchyIndex;}
private:
    void release();

    DiagnosticEmitterBase& d;
    DiagnosticPathNode* prev;
    QString pathName;
    QString detailedName;
    int hierarchyIndex;
};

class DiagnosticEmitterBase
{
    friend class DiagnosticPathNode;
public:
    virtual ~DiagnosticEmitterBase(){}

    /**
     * @brief setDetailedName set detailed name on last pushed node
     * @param name the detailed name to put
     */
    void setDetailedName(const QString& name){Q_ASSERT(head); head->setDetailedName(name);}

    template<typename... Args>
    void operator()(Diag::ID id, Args&&... arg){
        QList<QVariant> params;
        appendParam(params, std::forward<Args>(arg)...);
        diagnosticHandle(id, params);
    }
    template<>
    void operator()(Diag::ID id){
        diagnosticHandle(id, QList<QVariant>());
    }

    // the form that directly passes the arguments
    void handle(Diag::ID id, const QList<QVariant>& arg){
        diagnosticHandle(id, arg);
    }

protected:
    /**
     * @brief currentHead get most recently pushed node. Intended for child
     * @return pointer to path node; null if no node pushed
     */
    const DiagnosticPathNode* currentHead(){return head;}

private:
    // we now only accept following types as parameter to diagnostic
    void appendParam(QList<QVariant>& param, ValueType val){
        QVariant v;
        v.setValue(ValueTypeWrapper{val});
        param.push_back(v);
    }
    void appendParam(QList<QVariant>& param, int val){
        param.push_back(QVariant(val));
    }
    void appendParam(QList<QVariant>& param, const QString& val){
        param.push_back(QVariant(val));
    }
    void appendParam(QList<QVariant>& param, const QStringList& val){
        param.push_back(val);
    }
    void appendParam(QList<QVariant>& param, const StringDiagnosticRecord& val){
        QVariant v;
        Q_ASSERT(val.infoStart >= 0);
        Q_ASSERT(val.infoStart <= val.infoEnd);
        Q_ASSERT(val.infoEnd <= val.str.length());
        Q_ASSERT(val.errorStart >= 0);
        Q_ASSERT(val.errorStart <=val.errorEnd);
        Q_ASSERT(val.errorEnd <= val.str.length());
        v.setValue(val);
        param.push_back(v);
    }
    template<typename T, typename... Args>
    void appendParam(QList<QVariant>& param, const T& val, Args&&... arg){
        appendParam(param, val);
        appendParam(param, std::forward<Args>(arg)...);
    }

protected:
    virtual void diagnosticHandle(Diag::ID id, const QList<QVariant>& data){Q_UNUSED(id) Q_UNUSED(data)}
private:
    DiagnosticPathNode* head = nullptr;
    int hierarchyCount = 0;
};

// just for testing purpose; we will have GUI oriented implementation later on
class ConsoleDiagnosticEmitter: public DiagnosticEmitterBase
{
public:
    virtual ~ConsoleDiagnosticEmitter() override {}
    virtual void diagnosticHandle(Diag::ID id, const QList<QVariant>& data) override;
};

/*
class DiagnosticEmitter : public QObject
{
    Q_OBJECT
public:
    explicit DiagnosticEmitter(QObject *parent = nullptr);

signals:

public slots:
};
*/

#endif // DIAGNOSTICEMITTER_H
