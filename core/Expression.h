#ifndef EXPRESSION_H
#define EXPRESSION_H

#include <QtGlobal>
#include <QString>
#include <QVariant>
#include <QList>

#include "core/Value.h"

class ExecutionContext;

/**
 * @brief The ExpressionBase class is an interface for any expression that do not have side effects
 */
class ExpressionBase
{
    Q_DECLARE_TR_FUNCTIONS(ExpressionBase)
public:
    virtual ~ExpressionBase(){}

    virtual ValueType getExpressionType() const = 0;
    /**
     * @brief getDependency get list of expression indices that this expression depends upon
     * @param dependentExprIndexList
     */
    virtual void getDependency(QList<int>& dependentExprIndexList, QList<ValueType>& exprTypeList) const{
        Q_UNUSED(dependentExprIndexList)
        Q_UNUSED(exprTypeList)
    }
    /**
     * @brief evaluate evaluates the expression
     * @param ctx the environment of evaluation
     * @param retVal the return value
     * @param dependentExprResults dependent expression evaluation results
     * @return true if execution is successful; false if there is any fatal error that should abort the evaluation
     */
    virtual bool evaluate(ExecutionContext& ctx, QVariant& retVal, const QList<QVariant>& dependentExprResults) const = 0;

    /**
     * @brief emitTypeErrorDiagnostic generates an error on type mismatch
     * @param ctx the environment of evaluation
     * @param expectedTy expected type of an expression
     * @param actualTy actual type of an expression
     * @param exprText text to describe the expression having unexpected type
     */
    void emitTypeErrorDiagnostic(ExecutionContext& ctx, ValueType expectedTy, ValueType actualTy, QString exprText = QString()) const;
};

/**
 * @brief The LiteralExpression class
 *
 * Literal value as expression
 */
class LiteralExpression: public ExpressionBase
{
public:
    explicit LiteralExpression(ValueType ty, QVariant val)
        : ty(ty), val(val)
    {}
    explicit LiteralExpression(qint64 val)
        : ty(ValueType::Int64),
          val(val)
    {}
    explicit LiteralExpression(QString str)
        : ty(ValueType::String),
          val(str)
    {}
    virtual ~LiteralExpression() override{}
    virtual ValueType getExpressionType() const override {return ty;}
    virtual bool evaluate(ExecutionContext& ctx, QVariant& retVal, const QList<QVariant>& dependentExprResults) const override;
private:
    ValueType ty;
    QVariant val;
};

/**
 * @brief The VariableAddressExpression class
 *
 * Create a pointer from variable name lookup
 */
class VariableAddressExpression: public ExpressionBase
{
public:
    explicit VariableAddressExpression(QString varName)
        : variableName(varName)
    {}
    virtual ~VariableAddressExpression() override {}
    virtual ValueType getExpressionType() const override {return ValueType::ValuePtr;}
    virtual bool evaluate(ExecutionContext& ctx, QVariant& retVal, const QList<QVariant>& dependentExprResults) const override;
private:
    QString variableName;
};

/**
 * @brief The VariableReadExpression class
 *
 * Just read the value by name
 */
class VariableReadExpression: public ExpressionBase
{
public:
    explicit VariableReadExpression(ValueType ty, QString varName)
        : ty(ty), variableName(varName)
    {}
    virtual ~VariableReadExpression() override {}
    virtual ValueType getExpressionType() const override {return ty;}
    virtual bool evaluate(ExecutionContext& ctx, QVariant& retVal, const QList<QVariant>& dependentExprResults) const override;
private:
    ValueType ty;
    QString variableName;
};

/**
 * @brief The NodeReferenceExpression class
 *
 * Create a node pointer
 */
class NodePtrExpression: public ExpressionBase
{
public:
    enum class NodeSpecifier{
        CurrentNode,
        RootNode
    };
    explicit NodePtrExpression(NodeSpecifier specifier)
        : specifier(specifier)
    {}
    virtual ~NodePtrExpression() override {}
    virtual ValueType getExpressionType() const override {return ValueType::NodePtr;}
    virtual bool evaluate(ExecutionContext& ctx, QVariant& retVal, const QList<QVariant>& dependentExprResults) const override;
private:
    NodeSpecifier specifier;
};

#endif // EXPRESSION_H