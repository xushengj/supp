#include "core/Bundle.h"

#include "core/DiagnosticEmitter.h"
#include "core/Expression.h"
#include "core/IR.h"
#include "core/Task.h"
#include "core/CLIDriver.h"
#include "core/OutputHandlerBase.h"
#include "core/ExecutionContext.h"

#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QDebug>

#include <stdexcept>
#include <memory>

namespace{

const QString STR_NAME               = QStringLiteral("Name");
const QString STR_TYPE               = QStringLiteral("Type");
const QString STR_TY_INT             = QStringLiteral("Int");
const QString STR_TY_STRING          = QStringLiteral("String");
const QString STR_TY_NODEPTR         = QStringLiteral("NodePtr");
const QString STR_TY_VALUEPTR        = QStringLiteral("ValuePtr");
const QString STR_EXPR_TYPE          = QStringLiteral("ExprType");
const QString STR_EXPR_TYPE_LITERAL  = QStringLiteral("Literal");
const QString STR_EXPR_TYPE_VAR_READ = QStringLiteral("VariableRead");
const QString STR_EXPR_TYPE_VAR_ADDR = QStringLiteral("VariableAddress");
const QString STR_EXPR_LITERAL_VALUE = QStringLiteral("LiteralValue");
const QString STR_EXPR_VAR_NAME      = QStringLiteral("VariableName");
const QString STR_DECL_INITIALIZER   = QStringLiteral("Initializer");
const QString STR_FUNCTION_PARAM_REQ = QStringLiteral("ParameterRequired");
const QString STR_FUNCTION_PARAM_OPT = QStringLiteral("ParameterOptional");
const QString STR_FUNCTION_LOCALVAR  = QStringLiteral("LocalVariable");
const QString STR_FUNCTION_EXTVARREF = QStringLiteral("ExternVariableReference");
const QString STR_FUNCTION_STMT      = QStringLiteral("Statement");
const QString STR_STMT_UNREACHABLE   = QStringLiteral("Unreachable");
const QString STR_STMT_ASSIGN        = QStringLiteral("Assignment");
const QString STR_STMT_ASSIGN_LHS    = QStringLiteral("AssignmentLHS");
const QString STR_STMT_ASSIGN_RHS    = QStringLiteral("AssignmentRHS");
const QString STR_STMT_OUTPUT        = QStringLiteral("Output");
const QString STR_STMT_OUTPUT_EXPR   = QStringLiteral("OutputExpr");
const QString STR_STMT_CALL          = QStringLiteral("Call");
const QString STR_STMT_CALL_FUNC     = QStringLiteral("CallFunction");
const QString STR_STMT_CALL_ARG      = QStringLiteral("CallArgument");
const QString STR_STMT_RETURN        = QStringLiteral("Return");
const QString STR_STMT_BRANCH        = QStringLiteral("Branch");
const QString STR_STMT_BRANCH_D      = QStringLiteral("BranchDefault");
const QString STR_STMT_BRANCH_CASE   = QStringLiteral("BranchCase");
const QString STR_STMT_BRANCH_ACTTY  = QStringLiteral("ActionType");
const QString STR_STMT_BRANCH_UR     = QStringLiteral("Unreachable");
const QString STR_STMT_BRANCH_FT     = QStringLiteral("Fallthrough");
const QString STR_STMT_BRANCH_J      = QStringLiteral("Jump");
const QString STR_STMT_BRANCH_LABEL  = QStringLiteral("Label");
const QString STR_STMT_BRANCH_COND   = QStringLiteral("Condition");
const QString STR_STMT_BRANCH_ACT    = QStringLiteral("Action");
const QString STR_STMT_LABEL         = QStringLiteral("LabelPseudoStatement");
const QString STR_STMT_LABEL_NAME    = QStringLiteral("LabelName");
const QString STR_IRNODE_PARAM       = QStringLiteral("Parameter");
const QString STR_IRNODE_PARAM_UNIQUE= QStringLiteral("Unique");
const QString STR_IRNODE_KEY         = QStringLiteral("PrimaryKey");
const QString STR_IRNODE_CHILD       = QStringLiteral("Child");
const QString STR_IRROOT_NODE        = QStringLiteral("Node");
const QString STR_IRROOT_ROOT        = QStringLiteral("Root");
const QString STR_TOP_IRSET          = QStringLiteral("IRSet");
const QString STR_TOP_OUTPUTSET      = QStringLiteral("OutputSet");
const QString STR_TOP_TASKSET        = QStringLiteral("TaskSet");
const QString STR_OUTPUT_BASETYPE    = QStringLiteral("BaseType");
const QString STR_OUTPUT_TEXT_MIME   = QStringLiteral("TextMIME");
const QString STR_OUTPUT_TEXT_CODEC  = QStringLiteral("TextCodec");
const QString STR_TASK_INPUT         = QStringLiteral("Input");
const QString STR_TASK_OUTPUT        = QStringLiteral("Output");
const QString STR_TASK_GLOBALVAR     = QStringLiteral("GlobalVariable");
const QString STR_TASK_NODEMEMBER    = QStringLiteral("NodeMember");
const QString STR_TASK_FUNCTION      = QStringLiteral("Function");
const QString STR_TASK_PASS          = QStringLiteral("Pass");
const QString STR_TASK_PASS_ONENTRY  = QStringLiteral("OnEntry");
const QString STR_TASK_PASS_ONEXIT   = QStringLiteral("OnExit");
const QString STR_INSTANCE_PARENT    = QStringLiteral("Parent");
const QString STR_INSTANCE_PARAM     = QStringLiteral("Parameter");

// for all functions, we throw when any error is encountered
// use std::unique_ptr to avoid memory leak
ValueType getValueTypeFromString(DiagnosticEmitterBase& diagnostic, const QString& ty){
    if(ty == STR_TY_INT)
        return ValueType::Int64;
    if(ty == STR_TY_STRING)
        return ValueType::String;
    if(ty == STR_TY_NODEPTR)
        return ValueType::NodePtr;
    if(ty == STR_TY_VALUEPTR)
        return ValueType::ValuePtr;

    diagnostic.error(Bundle::tr("Unknown type"),
                     Bundle::tr("Given type string is not known"),
                     ty);
    throw std::runtime_error("Unknown type");
}

int getExpression(DiagnosticEmitterBase& diagnostic, const QJsonObject& json, Function& f)
{
    Q_UNUSED(f)
    QString exprTy = json.value(STR_EXPR_TYPE).toString();
    if(exprTy == STR_EXPR_TYPE_LITERAL){
        QJsonValue val = json.value(STR_EXPR_LITERAL_VALUE);
        if(val.isString()){

            return f.addExpression(new LiteralExpression(val.toString()));
        }else if(val.isDouble()){
            int value = val.toInt();
            return f.addExpression(new LiteralExpression(static_cast<qint64>(value)));
        }else{
            diagnostic.error(QString(),
                             Bundle::tr("Unhandled literal type"));
            throw std::runtime_error("Unhandled literal type");
        }
    }else if(exprTy == STR_EXPR_TYPE_VAR_READ){
        QString name = json.value(STR_EXPR_VAR_NAME).toString();
        ValueType ty = ValueType::Void;
        int refIndex = f.getLocalVariableIndex(name);
        if(refIndex >= 0){
            ty = f.getLocalVariableType(refIndex);
        }else{
            refIndex = f.getExternVariableIndex(name);
            if(Q_UNLIKELY(refIndex == -1)){
                diagnostic.error(QString(),
                                 Bundle::tr("Reference to unknown variable"),
                                 name);
                throw std::runtime_error("Unknown variable");
            }
            ty = f.getExternVariableType(refIndex);
        }
        return f.addExpression(new VariableReadExpression(ty, name));
    }else if(exprTy == STR_EXPR_TYPE_VAR_ADDR){
        QString name = json.value(STR_EXPR_VAR_NAME).toString();
        return f.addExpression(new VariableAddressExpression(name));
    }

    diagnostic.error(Bundle::tr("Unknown expression type"),
                     Bundle::tr("Given expression type string is not known"),
                     exprTy);
    throw std::runtime_error("Unknown expression");
}

struct MemberDeclarationEntry{
    QString name;
    ValueType ty;
    QVariant initializer;
};

void getMemberDeclaration(DiagnosticEmitterBase& diagnostic, const QJsonArray& json, QList<MemberDeclarationEntry>& members)
{
    for(auto iter = json.begin(), iterEnd = json.end(); iter != iterEnd; ++iter){
        QJsonObject obj = iter->toObject();
        MemberDeclarationEntry entry;
        entry.name = obj.value(STR_NAME).toString();
        entry.ty = getValueTypeFromString(diagnostic, obj.value(STR_TYPE).toString());
        entry.initializer = QVariant();
        QJsonValue initializer = obj.value(STR_DECL_INITIALIZER);
        if(!initializer.isUndefined()){
            switch (entry.ty) {
            default:{
                diagnostic.error(Bundle::tr("Unhandled initializer type"),
                                 Bundle::tr("Variable of given type cannot have initializer"), // if we reach here, getValueTypeFromString() didn't throw
                                 getTypeNameString(entry.ty));
                throw std::runtime_error("Unhandled initializer type");
            }/*break;*/
            case ValueType::Int64:
                entry.initializer = initializer.toInt();
                break;
            case ValueType::String:
                entry.initializer = initializer.toString();
                break;
            }
        }
        members.push_back(entry);
    }
}

Function getFunction(DiagnosticEmitterBase& diagnostic, const QJsonObject& json)
{
    QString name = json.value(STR_NAME).toString();
    diagnostic.pushNode(name);
    Function func(name);
    // parameters and local variables
    {
        QList<MemberDeclarationEntry> vars;
        getMemberDeclaration(diagnostic, json.value(STR_FUNCTION_PARAM_REQ).toArray(), vars);
        for(const auto& record : vars){
            func.addLocalVariable(record.name, record.ty, record.initializer);
        }
        int requiredParamCount = vars.size();
        func.setRequiredParamCount(requiredParamCount);
        vars.clear();
        getMemberDeclaration(diagnostic, json.value(STR_FUNCTION_PARAM_OPT).toArray(), vars);
        for(const auto& record : vars){
            func.addLocalVariable(record.name, record.ty, record.initializer);
        }
        func.setParamCount(requiredParamCount + vars.size());
        vars.clear();
        getMemberDeclaration(diagnostic, json.value(STR_FUNCTION_LOCALVAR).toArray(), vars);
        for(const auto& record : vars){
            func.addLocalVariable(record.name, record.ty, record.initializer);
        }
        vars.clear();
        getMemberDeclaration(diagnostic, json.value(STR_FUNCTION_EXTVARREF).toArray(), vars);
        for(const auto& record : vars){
            func.addExternVariable(record.name, record.ty);
        }
        vars.clear();
    }
    // statements and labels
    QJsonArray array = json.value(STR_FUNCTION_STMT).toArray();
    for(auto iter = array.begin(), iterEnd = array.end(); iter != iterEnd; ++iter){
        QJsonObject obj = iter->toObject();
        QString stmtTy = obj.value(STR_TYPE).toString();
        if(stmtTy == STR_STMT_UNREACHABLE){
            func.addUnreachableStatement();
        }else if(stmtTy == STR_STMT_ASSIGN){
            QJsonValue lhs = obj.value(STR_STMT_ASSIGN_LHS);
            AssignmentStatement stmt;
            if(lhs.isString()){
                stmt.lvalueName = lhs.toString();
                stmt.lvalueExprIndex = -1;
            }else{
                stmt.lvalueExprIndex = getExpression(diagnostic, lhs.toObject(), func);
            }
            stmt.rvalueExprIndex = getExpression(diagnostic, obj.value(STR_STMT_ASSIGN_RHS).toObject(), func);
            func.addStatement(stmt);
        }else if(stmtTy == STR_STMT_OUTPUT){
            OutputStatement stmt;
            stmt.exprIndex = getExpression(diagnostic, obj.value(STR_STMT_OUTPUT_EXPR).toObject(), func);
            func.addStatement(stmt);
        }else if(stmtTy == STR_STMT_CALL){
            CallStatement stmt;
            stmt.functionName = obj.value(STR_STMT_CALL_FUNC).toString();
            QJsonArray args = obj.value(STR_STMT_CALL_ARG).toArray();
            for(auto iter = args.begin(), iterEnd = args.end(); iter != iterEnd; ++iter){
                stmt.argumentExprList.push_back(getExpression(diagnostic, iter->toObject(), func));
            }
            func.addStatement(stmt);
        }else if(stmtTy == STR_STMT_RETURN){
            func.addReturnStatement();
        }else if(stmtTy == STR_STMT_BRANCH){
            BranchStatementTemp stmt;
            auto setAction = [&](const QJsonObject& actionObject, BranchStatementTemp::BranchActionType& ty, QString& labelName)->void{
                QString actTy = actionObject.value(STR_STMT_BRANCH_ACTTY).toString();
                if(actTy == STR_STMT_BRANCH_J){
                    ty = BranchStatementTemp::BranchActionType::Jump;
                    labelName = actionObject.value(STR_STMT_BRANCH_LABEL).toString();
                }else if(actTy == STR_STMT_BRANCH_FT){
                    ty = BranchStatementTemp::BranchActionType::Fallthrough;
                    labelName.clear();
                }else if(actTy == STR_STMT_BRANCH_UR){
                    ty = BranchStatementTemp::BranchActionType::Unreachable;
                    labelName.clear();
                }else{
                    diagnostic.error(Bundle::tr("Unhandled branch action type"),
                                     Bundle::tr("Provided branch action type string is not recognized"),
                                     actTy);
                    throw std::runtime_error("Unhandled branch action type");
                }
            };
            setAction(obj.value(STR_STMT_BRANCH_D).toObject(), stmt.defaultAction, stmt.defaultJumpLabelName);
            QJsonArray caseArray = obj.value(STR_STMT_BRANCH_CASE).toArray();
            for(auto iter = caseArray.begin(), iterEnd = caseArray.end(); iter != iterEnd; ++iter){
                BranchStatementTemp::BranchCase c;
                QJsonObject caseObj = iter->toObject();
                c.exprIndex = getExpression(diagnostic, caseObj.value(STR_STMT_BRANCH_COND).toObject(), func);
                setAction(caseObj.value(STR_STMT_BRANCH_ACT).toObject(), c.action, c.labelName);
                stmt.cases.push_back(c);
            }
            func.addStatement(stmt);
        }else if(stmtTy == STR_STMT_LABEL){
            QString labelName = obj.value(STR_STMT_LABEL_NAME).toString();
            func.addLabel(labelName);
        }else{
            diagnostic.error(Bundle::tr("Unhandled statement type"),
                             Bundle::tr("Provided statement type is unknown"),
                             stmtTy);
            throw std::runtime_error("Unhandled statement type");
        }
    }

    diagnostic.popNode();
    return func;
}

IRNodeType* getIRNodeType(DiagnosticEmitterBase& diagnostic, const QJsonObject& json)
{
    QString name = json.value(STR_NAME).toString();
    diagnostic.pushNode(name);
    std::unique_ptr<IRNodeType> ptr(new IRNodeType(name));
    QJsonArray param = json.value(STR_IRNODE_PARAM).toArray();
    for(auto iter = param.begin(), iterEnd = param.end(); iter != iterEnd; ++iter){
        QJsonObject entry = iter->toObject();
        QString paramName = entry.value(STR_NAME).toString();
        ValueType paramTy = getValueTypeFromString(diagnostic, entry.value(STR_TYPE).toString());
        bool isUnique = entry.value(STR_IRNODE_PARAM_UNIQUE).toBool(false);
        ptr->addParameter(paramName, paramTy, isUnique);
    }
    QJsonValue primaryKeyVal = json.value(STR_IRNODE_KEY);
    if(primaryKeyVal.isString()){
        ptr->setPrimaryKey(primaryKeyVal.toString());
    }
    QJsonArray child = json.value(STR_IRNODE_CHILD).toArray();
    for(auto c: child){
        ptr->addChildNode(c.toString());
    }
    diagnostic.popNode();
    return ptr.release();
}

IRRootType* getIRRootType(DiagnosticEmitterBase& diagnostic, const QJsonObject& json)
{
    QString name = json.value(STR_NAME).toString();
    diagnostic.pushNode(name);
    std::unique_ptr<IRRootType> ptr(new IRRootType(name));
    QJsonArray nodeArray = json.value(STR_IRROOT_NODE).toArray();
    for(auto node: nodeArray){
        IRNodeType* nodePtr = getIRNodeType(diagnostic, node.toObject());
        ptr->addNodeTypeDefinition(*nodePtr);
        delete nodePtr;
    }
    ptr->setRootNodeType(json.value(STR_IRROOT_ROOT).toString());
    diagnostic.popNode();
    return ptr.release();
}

}// end of anonymous namespace

Bundle* Bundle::fromJson(const QByteArray& json, DiagnosticEmitterBase &diagnostic)
{
    QJsonDocument doc = QJsonDocument::fromJson(json);
    QJsonObject docObj = doc.object();
    if(docObj.isEmpty()){
        qDebug()<< "json read fail";
        return nullptr;
    }
    try {
        std::unique_ptr<Bundle> ptr(new Bundle);
        // IR first
        diagnostic.pushNode(tr("IR"));
        QJsonArray irArray = docObj.value(STR_TOP_IRSET).toArray();
        for(auto ir : irArray){
            IRRootType* irRoot = getIRRootType(diagnostic, ir.toObject());
            if(irRoot->validate(diagnostic)){
                int index = ptr->irTypes.size();
                ptr->irTypes.push_back(irRoot);
                ptr->irNameToIndex.insert(irRoot->getName(), index);
            }else{
                delete irRoot;
                return nullptr;
            }

        }
        diagnostic.popNode();//IR

        // then outputs
        QJsonArray outputArray = docObj.value(STR_TOP_OUTPUTSET).toArray();
        for(auto iter = outputArray.begin(), iterEnd = outputArray.end(); iter != iterEnd; ++iter){
            QJsonObject obj = iter->toObject();
            OutputDescriptor out;
            out.name = obj.value(STR_NAME).toString();
            out.baseTy = OutputDescriptor::OutputBaseType::Text;
            out.textInfo.mimeType = obj.value(STR_OUTPUT_TEXT_MIME).toString();
            out.textInfo.codecName = obj.value(STR_OUTPUT_TEXT_CODEC).toString(QStringLiteral("utf-8"));
            int outputIndex = ptr->outputTypes.size();
            ptr->outputTypes.push_back(out);
            ptr->outputNameToIndex.insert(out.name, outputIndex);
        }

        // then tasks
        QJsonArray taskArray = docObj.value(STR_TOP_TASKSET).toArray();
        for(auto iter = taskArray.begin(), iterEnd = taskArray.end(); iter != iterEnd; ++iter){
            QJsonObject obj = iter->toObject();
            QString inputIRName = obj.value(STR_TASK_INPUT).toString();
            QString outputName = obj.value(STR_TASK_OUTPUT).toString();
            int irIndex = -1;
            {
                auto iter = ptr->irNameToIndex.find(inputIRName);
                if(Q_LIKELY(iter != ptr->irNameToIndex.end())){
                    irIndex = iter.value();
                }
            }
            if(Q_UNLIKELY(irIndex == -1)){
                diagnostic.error(QString(),
                                 tr("Unknown IR name"),
                                 inputIRName);
                return nullptr;
            }
            TaskRecord record;
            record.ptr = nullptr;
            record.inputIRType = irIndex;
            record.outputTypeIndex = ptr->outputNameToIndex.value(outputName, -1);
            if(Q_UNLIKELY(record.outputTypeIndex == -1)){
                diagnostic.error(QString(),
                                 tr("Unknown output name"),
                                 outputName);
                return nullptr;
            }
            const IRRootType& irRoot = *ptr->irTypes.at(irIndex);
            std::unique_ptr<Task> taskPtr(new Task(irRoot));
            QJsonArray functionArray = obj.value(STR_TASK_FUNCTION).toArray();
            for(auto iter = functionArray.begin(), iterEnd = functionArray.end(); iter != iterEnd; ++iter){
                taskPtr->addFunction(getFunction(diagnostic, iter->toObject()));
            }
            QList<MemberDeclarationEntry> vars;
            getMemberDeclaration(diagnostic, obj.value(STR_TASK_GLOBALVAR).toArray(), vars);
            for(const auto& record : vars){
                taskPtr->addGlobalVariable(record.name, record.ty, record.initializer);
            }
            vars.clear();
            QJsonObject nodeMember = obj.value(STR_TASK_NODEMEMBER).toObject();
            for(auto iter = nodeMember.begin(), iterEnd = nodeMember.end(); iter != iterEnd; ++iter){
                QString nodeName = iter.key();
                int nodeIndex = irRoot.getNodeTypeIndex(nodeName);
                if(Q_UNLIKELY(nodeIndex < 0)){
                    diagnostic.error(QString(),
                                     tr("Unknown node name"),
                                     nodeName);
                    return nullptr;
                }
                getMemberDeclaration(diagnostic, iter.value().toArray(), vars);
                for(const auto& record : vars){
                    taskPtr->addNodeMember(nodeIndex, record.name, record.ty, record.initializer);
                }
                vars.clear();
            }
            QJsonArray passArray = obj.value(STR_TASK_PASS).toArray();
            for(auto iter = passArray.begin(), iterEnd = passArray.end(); iter != iterEnd; ++iter){
                QJsonObject passObj = iter->toObject();
                taskPtr->addNewPass();
                for(auto iter = passObj.begin(), iterEnd = passObj.end(); iter != iterEnd; ++iter){
                    QString nodeName = iter.key();
                    int nodeIndex = irRoot.getNodeTypeIndex(nodeName);
                    if(Q_UNLIKELY(nodeIndex < 0)){
                        diagnostic.error(QString(),
                                         tr("Unknown node name"),
                                         nodeName);
                        return nullptr;
                    }
                    QJsonObject callbackObj = iter.value().toObject();
                    QJsonValue onEntryCB = callbackObj.value(STR_TASK_PASS_ONENTRY);
                    if(onEntryCB.isString()){
                        taskPtr->setNodeCallback(nodeIndex, onEntryCB.toString(), Task::CallbackType::OnEntry);
                    }
                    QJsonValue onExitCB = callbackObj.value(STR_TASK_PASS_ONEXIT);
                    if(onExitCB.isString()){
                        taskPtr->setNodeCallback(nodeIndex, onExitCB.toString(), Task::CallbackType::OnExit);
                    }
                }
            }
            if(taskPtr->validate(diagnostic)){
                ptr->taskInfo.push_back(record);
                ptr->taskInfo.back().ptr = taskPtr.release();
            }else{
                return nullptr;
            }
        }
        return ptr.release();
    } catch (...) {
        return nullptr;
    }
}

IRRootInstance* Bundle::readIRFromJson(int irIndex, const QByteArray& json, DiagnosticEmitterBase &diagnostic)
{
    QJsonDocument doc = QJsonDocument::fromJson(json);
    QJsonArray nodeArray = doc.array();
    if(nodeArray.isEmpty()){
        qDebug()<< "json read fail";
        return nullptr;
    }
    try {
        const IRRootType& rootTy = *irTypes.at(irIndex);
        std::unique_ptr<IRRootInstance> ptr(new IRRootInstance(rootTy));
        for(auto iter = nodeArray.begin(), iterEnd = nodeArray.end(); iter != iterEnd; ++iter){
            QJsonObject node = iter->toObject();
            QString nodeTypeName = node.value(STR_TYPE).toString();
            int typeIndex = rootTy.getNodeTypeIndex(nodeTypeName);
            int nodeIndex = ptr->addNode(typeIndex);
            IRNodeInstance& inst = ptr->getNode(nodeIndex);
            int parentIndex = node.value(STR_INSTANCE_PARENT).toInt();
            inst.setParent(parentIndex);
            inst.setParameters(node.value(STR_INSTANCE_PARAM).toArray().toVariantList());
            if(parentIndex >= 0){
                ptr->getNode(parentIndex).addChildNode(nodeIndex);
            }
        }
        if(ptr->validate(diagnostic)){
            return ptr.release();
        }
        return nullptr;
    } catch (...) {
        return nullptr;
    }
}

void testerEntry(){
    Bundle* ptr = nullptr;
    IRRootInstance* instPtr = nullptr;
    {
        QFile f(QStringLiteral("../test.json"));
        if(!f.open(QIODevice::ReadOnly)){
            qDebug()<< "test.json open failed";
            return;
        }
        QByteArray ba = f.readAll();
        f.close();
        ConsoleDiagnosticEmitter diagnostic;
        ptr = Bundle::fromJson(ba, diagnostic);
        if(ptr){
            qDebug()<<"bundle read success";
        }else{
            qDebug()<<"bundle read fail";
            return;
        }
    }
    {
        QFile f(QStringLiteral("../instance.json"));
        if(!f.open(QIODevice::ReadOnly)){
            qDebug()<< "instance.json open failed";
            return;
        }
        QByteArray ba = f.readAll();
        f.close();
        ConsoleDiagnosticEmitter diagnostic;
        instPtr = ptr->readIRFromJson(0, ba, diagnostic);
        if(instPtr){
            qDebug()<<"instance read success";
        }else{
            qDebug()<<"instance read fail";
            return;
        }
    }
    TextOutputHandler handler("utf-8");
    const Task& t = ptr->getTask(0);

    ConsoleDiagnosticEmitter diagnostic;
    ExecutionContext* ctx = new ExecutionContext(t, *instPtr, diagnostic, handler);
    ctx->continueExecution();
    delete ctx;

    qDebug()<< handler.getResult();


}
