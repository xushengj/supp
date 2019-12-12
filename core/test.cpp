#include "core/XML.h"
#include "core/Bundle.h"
#include "core/DiagnosticEmitter.h"
#include "core/Expression.h"
#include "core/IR.h"
#include "core/Task.h"
#include "core/CLIDriver.h"
#include "core/OutputHandlerBase.h"
#include "core/ExecutionContext.h"
#include "core/Parser.h"

#include <QDebug>

#include <stdexcept>
#include <memory>

void testWriteXML(){
    ConsoleDiagnosticEmitter diag;
    IRRootType* ty = new IRRootType("test");
    {
        IRNodeType speech("speech");
        speech.addParameter("character", ValueType::String, false);
        speech.addParameter("dummy", ValueType::Int64, false);
        speech.addParameter("text", ValueType::String, false);
        IRNodeType back("back");
        back.addParameter("text", ValueType::String, false);
        IRNodeType root("root");
        root.addChildNode("speech");
        root.addChildNode("back");
        ty->addNodeTypeDefinition(root);
        ty->addNodeTypeDefinition(speech);
        ty->addNodeTypeDefinition(back);
        ty->setRootNodeType("root");
    }
    Q_ASSERT(ty->validate(diag));
    int speechTyIndex = ty->getNodeTypeIndex("speech");
    int backTyIndex = ty->getNodeTypeIndex("back");

    IRRootInstance* inst = new IRRootInstance(*ty);
    int rootIdx = inst->addNode(ty->getNodeTypeIndex("root"));
    auto& root = inst->getNode(rootIdx);
    int s1 = inst->addNode(speechTyIndex);
    {
        auto& s1n = inst->getNode(s1);
        s1n.setParent(rootIdx);
        root.addChildNode(s1);
        QList<QVariant> args;
        args.push_back(QVariant("TA"));
        args.push_back(QVariant(0ll));
        args.push_back(QVariant("Hello world!\nUmm.."));
        s1n.setParameters(args);
    }
    int b1 = inst->addNode(backTyIndex);
    {
        auto& b1n = inst->getNode(b1);
        b1n.setParent(rootIdx);
        root.addChildNode(b1);
        QList<QVariant> args;
        args.push_back(QVariant(""));
        b1n.setParameters(args);
    }
    Q_ASSERT(inst->validate(diag));
    QFile f("test.txt");
    Q_ASSERT(f.open(QIODevice::WriteOnly));
    XML::writeIRInstance(*inst, &f);
    f.close();
    Q_ASSERT(f.open(QIODevice::ReadOnly));
    IRRootInstance* readBack = XML::readIRInstance(*ty, diag, &f);
    Q_ASSERT(readBack != nullptr);
    f.close();
    f.setFileName("test2.txt");
    Q_ASSERT(f.open(QIODevice::WriteOnly));
    XML::writeIRInstance(*readBack, &f);
    f.close();
    delete readBack;
    delete inst;
    delete ty;
}

void testParser(){
    ConsoleDiagnosticEmitter diag;
    IRRootType* ty = new IRRootType("test");
    {
        IRNodeType speech("speech");
        speech.addParameter("character", ValueType::String, false);
        speech.addParameter("dummy", ValueType::Int64, false);
        speech.addParameter("text", ValueType::String, false);
        IRNodeType back("back");
        back.addParameter("text", ValueType::String, false);
        IRNodeType root("root");
        root.addChildNode("speech");
        root.addChildNode("back");
        ty->addNodeTypeDefinition(root);
        ty->addNodeTypeDefinition(speech);
        ty->addNodeTypeDefinition(back);
        ty->setRootNodeType("root");
    }
    Q_ASSERT(ty->validate(diag));
    ParserPolicy policy;
    policy.name = QStringLiteral("TestParser");
    ParserPolicy::MatchPairRecord quote;
    quote.name = QStringLiteral("Quote");
    QString quoteStr = QStringLiteral("\"");
    quote.startEquivalentSet.push_back(quoteStr);
    quote.endEquivalentSet.push_back(quoteStr);
    policy.matchPairs.push_back(quote);
    policy.exprStartMark = QStringLiteral("<");
    policy.exprEndMark = QStringLiteral(">");
    policy.ignoreList.push_back(QStringLiteral(" "));
    policy.rootParserNodeName = QStringLiteral("root");


    ParserNode speechNode;
    speechNode.name = QStringLiteral("speech");
    ParserNode backNode;
    backNode.name = QStringLiteral("back");
    ParserNode speechNode2;
    speechNode2.name = QStringLiteral("speech2");
    {
        ParserNode rootNode;
        rootNode.name = policy.rootParserNodeName;
        rootNode.childNodeNameList.push_back(speechNode.name);
        rootNode.childNodeNameList.push_back(backNode.name);
        rootNode.childNodeNameList.push_back(speechNode2.name);
        policy.nodes.push_back(rootNode);
    }
    speechNode.parameterNameList.push_back(QStringLiteral("character"));
    speechNode.parameterNameList.push_back(QStringLiteral("dummy"));
    speechNode.parameterNameList.push_back(QStringLiteral("text"));
    speechNode2.parameterNameList.push_back(QStringLiteral("text"));
    backNode.parameterNameList.push_back(QStringLiteral("text"));
    {
        ParserNode::Pattern speechPattern;
        speechPattern.patternString = QStringLiteral("<character>:\"<text>\"");
        ParserNode::Pattern::ParamValueOverwriteRecord dummyRecord;
        dummyRecord.paramName = QStringLiteral("dummy");
        dummyRecord.valueExpr = QStringLiteral("1");
        speechPattern.valueOverwriteList.push_back(dummyRecord);
        speechNode.patterns.push_back(speechPattern);
        policy.nodes.push_back(speechNode);
    }
    {
        ParserNode::Pattern speech2Pattern;
        speech2Pattern.patternString = QStringLiteral("\"<text>\"");
        speechNode2.patterns.push_back(speech2Pattern);
        speechNode2.combineToNodeTypeName = QStringLiteral("speech");
        QStringList dummyList;
        dummyList.push_back(QStringLiteral("2"));
        QStringList characterValList;
        characterValList.push_back(QStringLiteral("<../speech[-0].character>"));
        speechNode2.combinedNodeParams.insert(QStringLiteral("dummy"), dummyList);
        speechNode2.combinedNodeParams.insert(QStringLiteral("character"), characterValList);
        policy.nodes.push_back(speechNode2);
    }
    {
        ParserNode::Pattern backPattern;
        backPattern.patternString = QStringLiteral("<text>");
        backNode.patterns.push_back(backPattern);
        policy.nodes.push_back(backNode);
    }
    QFile f("policy.txt");
    Q_ASSERT(f.open(QIODevice::WriteOnly));
    QXmlStreamWriter xml(&f);
    xml.setAutoFormatting(true);
    xml.setAutoFormattingIndent(2);
    xml.writeStartDocument();
    XML::writeParser(policy, xml);
    xml.writeEndDocument();
    f.close();
    Parser* p = Parser::getParser(policy, *ty, diag);
    Q_ASSERT(p != nullptr);
    QString l1("TA:\"Hello guys...\"");
    QString l2("umm.. not many people is here.");
    QString l3("\"Okay lets get started\"");
    QVector<QStringRef> tu;
    tu.push_back(QStringRef(&l1));
    tu.push_back(QStringRef(&l2));
    tu.push_back(QStringRef(&l3));
    IRRootInstance* ir = p->parse(tu, *ty, diag);
    Q_ASSERT(ir != nullptr);
    f.setFileName("ir.txt");
    Q_ASSERT(f.open(QIODevice::WriteOnly));
    XML::writeIRInstance(*ir, &f);
    f.close();
}

void bundleTest(){
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

void testerEntry(){
    testParser();
    return;
}
