#include "core/Expression.h"

#include "core/DiagnosticEmitter.h"
#include "core/ExecutionContext.h"

bool LiteralExpression::evaluate(ExecutionContext& ctx, QVariant& retVal, const QList<QVariant>& dependentExprResults) const
{
    Q_UNUSED(ctx)
    Q_UNUSED(dependentExprResults)
    retVal = val;
    return true;
}

bool VariableAddressExpression::evaluate(ExecutionContext& ctx, QVariant& retVal, const QList<QVariant>& dependentExprResults) const
{
    Q_UNUSED(dependentExprResults)
    ValuePtrType val = {};
    if(ctx.takeAddress(variableName, val)){
        retVal.setValue(val);
        return true;
    }
    return false;
}

bool VariableReadExpression::evaluate(ExecutionContext& ctx, QVariant& retVal, const QList<QVariant>& dependentExprResults) const
{
    Q_UNUSED(dependentExprResults)
    ValueType actualTy;
    QVariant val;
    if(ctx.read(variableName, actualTy, val)){
        if(Q_LIKELY(actualTy == ty)){
            retVal = val;
            return true;
        }else{
            ctx.getDiagnostic()(Diag::Error_Exec_TypeMismatch_ReadByName, ty, actualTy, variableName);
            return false;
        }
    }
    // read fail; ctx should already emitted diagnostic
    return false;
}

bool NodePtrExpression::evaluate(ExecutionContext& ctx, QVariant& retVal, const QList<QVariant>& dependentExprResults) const
{
    Q_UNUSED(dependentExprResults)
    NodePtrType ptr = {};
    bool getNodeRetVal = false;
    switch(specifier){
    case NodeSpecifier::CurrentNode:
        getNodeRetVal = ctx.getCurrentNodePtr(ptr);
        break;
    case NodeSpecifier::RootNode:
        getNodeRetVal = ctx.getRootNodePtr(ptr);
        break;
    }
    Q_ASSERT(getNodeRetVal);
    retVal.setValue(ptr);
    return true;
}
