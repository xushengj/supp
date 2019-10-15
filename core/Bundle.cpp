#include "core/Bundle.h"

#include "core/DiagnosticEmitter.h"
#include "core/Expression.h"
#include "core/Task.h"

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
const QString STR_EXPR_VAR_READTYPE  = QStringLiteral("VariableReadType");
const QString STR_DECL_INITIALIZER   = QStringLiteral("Initializer");
const QString STR_FUNCTION_PARAM_REQ = QStringLiteral("ParameterRequired");
const QString STR_FUNCTION_PARAM_OPT = QStringLiteral("ParameterOptional");
const QString STR_FUNCTION_LOCALVAR  = QStringLiteral("LocalVariable");
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
        QString ty = json.value(STR_EXPR_VAR_READTYPE).toString();
        return f.addExpression(new VariableReadExpression(getValueTypeFromString(diagnostic,ty), name));
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

Function* getFunction(DiagnosticEmitterBase& diagnostic, const QJsonObject& json)
{
    QString name = json.value(STR_NAME).toString();
    diagnostic.pushNode(name);
    std::unique_ptr<Function> ptr(new Function(name));
    // parameters and local variables
    {
        QList<MemberDeclarationEntry> vars;
        getMemberDeclaration(diagnostic, json.value(STR_FUNCTION_PARAM_REQ).toArray(), vars);
        for(const auto& record : vars){
            ptr->addLocalVariable(record.name, record.ty, record.initializer);
        }
        int requiredParamCount = vars.size();
        ptr->setRequiredParamCount(requiredParamCount);
        vars.clear();
        getMemberDeclaration(diagnostic, json.value(STR_FUNCTION_PARAM_OPT).toArray(), vars);
        for(const auto& record : vars){
            ptr->addLocalVariable(record.name, record.ty, record.initializer);
        }
        ptr->setParamCount(requiredParamCount + vars.size());
        vars.clear();
        getMemberDeclaration(diagnostic, json.value(STR_FUNCTION_LOCALVAR).toArray(), vars);
        for(const auto& record : vars){
            ptr->addLocalVariable(record.name, record.ty, record.initializer);
        }
        vars.clear();
    }
    // statements and labels
    QJsonArray array = json.value(STR_FUNCTION_STMT).toArray();
    for(auto iter = array.begin(), iterEnd = array.end(); iter != iterEnd; ++iter){
        QJsonObject obj = iter->toObject();
        QString stmtTy = obj.value(STR_TYPE).toString();
        if(stmtTy == STR_STMT_UNREACHABLE){
            ptr->addUnreachableStatement();
        }else if(stmtTy == STR_STMT_ASSIGN){
            QJsonValue lhs = obj.value(STR_STMT_ASSIGN_LHS);
            AssignmentStatement stmt;
            if(lhs.isString()){
                stmt.lvalueName = lhs.toString();
                stmt.lvalueExprIndex = -1;
            }else{
                stmt.lvalueExprIndex = getExpression(diagnostic, lhs.toObject(), *ptr);
            }
            stmt.rvalueExprIndex = getExpression(diagnostic, obj.value(STR_STMT_ASSIGN_RHS).toObject(), *ptr);
            ptr->addStatement(stmt);
        }else if(stmtTy == STR_STMT_OUTPUT){
            OutputStatement stmt;
            stmt.exprIndex = getExpression(diagnostic, obj.value(STR_STMT_OUTPUT_EXPR).toObject(), *ptr);
            ptr->addStatement(stmt);
        }else if(stmtTy == STR_STMT_CALL){
            CallStatement stmt;
            stmt.functionName = obj.value(STR_STMT_CALL_FUNC).toString();
            QJsonArray args = obj.value(STR_STMT_CALL_ARG).toArray();
            for(auto iter = args.begin(), iterEnd = args.end(); iter != iterEnd; ++iter){
                stmt.argumentExprList.push_back(getExpression(diagnostic, iter->toObject(), *ptr));
            }
            ptr->addStatement(stmt);
        }else if(stmtTy == STR_STMT_RETURN){
            ptr->addReturnStatement();
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
                c.exprIndex = getExpression(diagnostic, caseObj.value(STR_STMT_BRANCH_COND).toObject(), *ptr);
                setAction(caseObj.value(STR_STMT_BRANCH_ACT).toObject(), c.action, c.labelName);
                stmt.cases.push_back(c);
            }
            ptr->addStatement(stmt);
        }else if(stmtTy == STR_STMT_LABEL){
            QString labelName = obj.value(STR_STMT_LABEL_NAME).toString();
            ptr->addLabel(labelName);
        }else{
            diagnostic.error(Bundle::tr("Unhandled statement type"),
                             Bundle::tr("Provided statement type is unknown"),
                             stmtTy);
            throw std::runtime_error("Unhandled statement type");
        }
    }

    diagnostic.popNode();
    return ptr.release();
}

}// end of anonymous namespace

void Bundle::readFromJson(const QByteArray& json)
{
    QJsonDocument doc = QJsonDocument::fromJson(json);
    if(doc.isEmpty()){
        qDebug()<< "json read fail";
        return;
    }


}
