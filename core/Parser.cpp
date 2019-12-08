#include "core/Parser.h"

#include "core/IR.h"
#include "core/DiagnosticEmitter.h"

#include <QQueue>
#include <QRegularExpressionMatch>
#include <QSet>
#include <QStack>
#include <QStringRef>

#include <algorithm>
#include <iterator>
#include <memory>

Parser* Parser::getParser(const ParserPolicy& policy, const IRRootType& ir, DiagnosticEmitterBase& diagnostic)
{
    bool isValidated = true;

    DiagnosticPathNode parserNode(diagnostic, tr("Parser"));
    if(Q_LIKELY(IRNodeType::validateName(diagnostic, policy.name))){
        parserNode.setDetailedName(policy.name);
    }else{
        isValidated = false;
    }

    std::unique_ptr<Parser> ptr(new Parser);

    // construct ParseContext

    // check match pairs:
    // 1. match pairs should have unique names (and the name must be good)
    // 2. match pair start strings can not be identical to any other match pair start strings.
    //    HOWEVER: match pair end strings can be identical to any other strings (both match pair start string or end string, even ignore string)
    //    Match pair end determination depends on context
    QHash<QString, int> matchPairStartToIndex;      //!< for each match pair start string, what's the match pair index
    QHash<QString, int> matchPairNameToIndex;       //!< for each match pair name, what's the index
    int longestMatchPairStartStringLength = 0;
    QList<int> matchPairScoreList;
    matchPairScoreList.reserve(policy.matchPairs.size());
    ptr->context.matchPairName.reserve(policy.matchPairs.size());
    ptr->context.matchPairStarts.reserve(policy.matchPairs.size());
    ptr->context.matchPairEnds.reserve(policy.matchPairs.size());
    for(int matchPairIndex = 0, n = policy.matchPairs.size(); matchPairIndex < n; ++matchPairIndex){
        const auto& mp = policy.matchPairs.at(matchPairIndex);
        const QString& name = mp.name;
        DiagnosticPathNode pathNode(diagnostic, tr("MatchPair %1").arg(matchPairIndex));
        if(Q_LIKELY(IRNodeType::validateName(diagnostic, name))){
            ptr->context.matchPairName.push_back(name);
            pathNode.setDetailedName(name);
            auto iter = matchPairNameToIndex.find(name);
            if(Q_LIKELY(iter == matchPairNameToIndex.end())){
                matchPairNameToIndex.insert(name, matchPairIndex);
            }else{
                diagnostic(Diag::Error_Parser_NameClash_MatchPair, name, iter.value(), matchPairIndex);
                isValidated = false;
            }
        }else{
            isValidated = false;
        }

        QStringList startList;
        int matchPairStartMinLength = 0;
        for(int i = 0, n = mp.startEquivalentSet.size(); i < n; ++i){
            const QString& start = mp.startEquivalentSet.at(i);
            if(Q_UNLIKELY(start.isEmpty())){
                diagnostic(Diag::Error_Parser_BadMatchPair_EmptyStartString, i);
                isValidated = false;
            }else{
                if(matchPairStartMinLength == 0 || matchPairStartMinLength > start.length()){
                    matchPairStartMinLength = start.length();
                }
                auto iter = matchPairStartToIndex.find(start);
                if(Q_LIKELY(iter == matchPairStartToIndex.end())){
                    matchPairStartToIndex.insert(start, i);
                    startList.push_back(start);

                    if(start.length() > longestMatchPairStartStringLength){
                        longestMatchPairStartStringLength = start.length();
                    }
                }else{
                    diagnostic(Diag::Error_Parser_BadMatchPair_StartStringConflict,
                               start, policy.matchPairs.at(iter.value()).name, iter.value(), name, i);
                    isValidated = false;
                }
            }
        }
        // for end marks, we only require that they are not redundant
        QStringList endList;
        int matchPairEndMinLength = 0;
        for(int i = 0, n = mp.endEquivalentSet.size(); i < n; ++i){
            const QString& end = mp.endEquivalentSet.at(i);
            if(Q_UNLIKELY(end.isEmpty())){
                diagnostic(Diag::Error_Parser_BadMatchPair_EmptyEndString, i);
                isValidated = false;
            }else{
                if (matchPairEndMinLength == 0 || matchPairEndMinLength > end.length()){
                    matchPairEndMinLength = end.length();
                }
                int firstIndex = endList.indexOf(end);
                if(Q_LIKELY(firstIndex == -1)){
                    endList.push_back(end);
                }else{
                    diagnostic(Diag::Error_Parser_BadMatchPair_EndStringDuplicated, matchPairIndex, end, firstIndex, i);
                    isValidated = false;
                }
            }
        }
        // also send an error if a match pair do not have start mark or end mark
        if(Q_UNLIKELY(matchPairStartMinLength == 0)){
            diagnostic(Diag::Error_Parser_BadMatchPair_NoStartString);
            isValidated = false;
        }
        if(Q_UNLIKELY(matchPairEndMinLength == 0)){
            diagnostic(Diag::Error_Parser_BadMatchPair_NoEndString);
            isValidated = false;
        }

        ptr->context.matchPairStarts.push_back(startList);
        ptr->context.matchPairEnds.push_back(endList);
        int matchPairScore = matchPairStartMinLength + matchPairEndMinLength;
        matchPairScoreList.push_back(matchPairScore);
    }
    ptr->context.longestMatchPairStartStringLength = longestMatchPairStartStringLength;

    ptr->context.ignoreList.reserve(policy.ignoreList.size());
    for(const QString& ignoreStr : policy.ignoreList){
        // remove duplicate or empty ones
        // maybe generate a warning instead? or other tools can simply drop them
        if(ignoreStr.isEmpty() || ptr->context.ignoreList.contains(ignoreStr)){
            continue;
        }
        ptr->context.ignoreList.push_back(ignoreStr);
    }

    // checks on exprStartMark and exprEndMark
    // requirement:
    // 1. none of the marker can be empty string
    // 2. none of the marker can be identical to one item in ignore list
    if(Q_UNLIKELY(policy.exprStartMark.isEmpty())){
        diagnostic(Diag::Error_Parser_BadExprMatchPair_EmptyStartString);
        isValidated = false;
    }else if(Q_UNLIKELY(ptr->context.ignoreList.contains(policy.exprStartMark))){
        diagnostic(Diag::Error_Parser_BadExprMatchPair_StartStringInIgnoreList);
        isValidated = false;
    }else{
        ptr->context.exprStartMark = policy.exprStartMark;
    }
    if(Q_UNLIKELY(policy.exprEndMark.isEmpty())){
        diagnostic(Diag::Error_Parser_BadExprMatchPair_EmptyEndString);
        isValidated = false;
    }else if(Q_UNLIKELY(ptr->context.ignoreList.contains(policy.exprEndMark))){
        diagnostic(Diag::Error_Parser_BadExprMatchPair_EndStringInIgnoreList);
        isValidated = false;
    }else{
        ptr->context.exprEndMark = policy.exprEndMark;
    }
    matchPairStartToIndex.clear();
    matchPairNameToIndex.clear();

    // start processing ParserNode
    QHash<QString, int> nodeNameToIndex;
    for(int i = 0, n = policy.nodes.size(); i < n; ++i){
        const ParserNode& src = policy.nodes.at(i);
        Node dest;
        DiagnosticPathNode pathNode(diagnostic, tr("Node %1").arg(i));

        if(Q_LIKELY(IRNodeType::validateName(diagnostic, src.name))){
            pathNode.setDetailedName(src.name);
            dest.nodeName = src.name;
            auto iter = nodeNameToIndex.find(src.name);
            if(Q_LIKELY(iter == nodeNameToIndex.end())){
                nodeNameToIndex.insert(src.name, i);
            }else{
                diagnostic(Diag::Error_Parser_NameClash_ParserNode, src.name, iter.value(), i);
                isValidated = false;
            }
        }else{
            isValidated = false;
        }

        dest.paramName.reserve(src.parameterNameList.size());
        for(int i = 0, n = src.parameterNameList.size(); i < n; ++i){
            const QString& paramName = src.parameterNameList.at(i);
            if(Q_LIKELY(IRNodeType::validateName(diagnostic, paramName))){
                int index = dest.paramName.indexOf(paramName);
                if(Q_LIKELY(index == -1)){
                    dest.paramName.push_back(paramName);
                }else{
                    diagnostic(Diag::Error_Parser_NameClash_ParserNodeParameter, paramName, index, i);
                    isValidated = false;
                }
            }else{
                isValidated = false;
            }
        }

        // validate and convert all patterns
        dest.patterns.reserve(src.patterns.size());
        for(int i = 0, n = src.patterns.size(); i < n; ++i){
            const ParserNode::Pattern& srcPattern = src.patterns.at(i);

            dest.patterns.push_back(Parser::Pattern());
            Parser::Pattern& destPattern = dest.patterns.back();
            DiagnosticPathNode pathNode(diagnostic, tr("Pattern %1").arg(i));

            QHash<QString, int> valueNameToIndex;
            if(Q_UNLIKELY(!ptr->context.parsePatternString(srcPattern.patternString, destPattern.elements, valueNameToIndex, diagnostic))){
                // skip this pattern
                isValidated = false;
                continue;
            }
            // set priority score
            int patternScore = srcPattern.priorityScore;
            if(patternScore == 0){
                patternScore = computePatternScore(destPattern.elements, matchPairScoreList);
            }
            destPattern.priorityScore = patternScore;

            QHash<QString, int> overwriteValueNameToIndex; // for each overwrite value, what's the overwrite record index
            QList<QList<PatternValueSubExpression>> overwriteValueTransformList;
            QSet<QString> referencedValues;
            for(int i = 0, n = srcPattern.valueOverwriteList.size(); i < n; ++i){
                DiagnosticPathNode pathNode(diagnostic, tr("Overwrite record %1").arg(i));
                const auto& record = srcPattern.valueOverwriteList.at(i);
                const QString& valueName = record.paramName;
                pathNode.setDetailedName(valueName);
                auto iter = overwriteValueNameToIndex.find(valueName);
                if(Q_LIKELY(iter == overwriteValueNameToIndex.end())){
                    overwriteValueNameToIndex.insert(valueName, i);
                }else{
                    diagnostic(Diag::Error_Parser_MultipleOverwrite, valueName, iter.value(), i);
                    isValidated = false;
                }
                overwriteValueTransformList.push_back(QList<PatternValueSubExpression>());
                if(Q_UNLIKELY(!ptr->context.parseValueTransformString(
                                  /* transform        */ record.valueExpr,
                                  /* result           */ overwriteValueTransformList.back(),
                                  /* referencedValues */ referencedValues,
                                  /* isLocalOnly      */ true,
                                  /* diagnostic       */ diagnostic))){
                    isValidated = false;
                }
            }

            // check if all ParserNode parameters are either overwritten or defined from pattern
            // give a warning if a parameter is not set
            destPattern.valueTransform.reserve(dest.paramName.size());
            for(int i = 0, n = dest.paramName.size(); i < n; ++i){
                const QString& paramName = dest.paramName.at(i);
                auto iter_overwrite = overwriteValueNameToIndex.find(paramName);
                if(iter_overwrite != overwriteValueNameToIndex.end()){
                    destPattern.valueTransform.push_back(overwriteValueTransformList.at(iter_overwrite.value()));
                    overwriteValueNameToIndex.erase(iter_overwrite);
                }else{
                    auto iter_def = valueNameToIndex.find(paramName);
                    if(Q_LIKELY(iter_def != valueNameToIndex.end())){
                        // append an empty transform
                        destPattern.valueTransform.push_back(QList<PatternValueSubExpression>());
                        referencedValues.insert(paramName);
                    }else{
                        diagnostic(Diag::Warn_Parser_MissingInitializer, paramName);
                        // initialize the parameter to an empty string
                        QList<PatternValueSubExpression> dummyExpr;
                        PatternValueSubExpression dummy;
                        dummy.ty = PatternValueSubExpression::OpType::Literal;
                        dummyExpr.push_back(dummy);
                        destPattern.valueTransform.push_back(dummyExpr);
                    }
                }
            }
            Q_ASSERT(destPattern.valueTransform.size() == dest.paramName.size());

            // if any defined overwrites or values are not referenced, issue a warning
            for(auto iter = valueNameToIndex.begin(), iterEnd = valueNameToIndex.end(); iter != iterEnd; ++iter){
                if(Q_UNLIKELY(!referencedValues.contains(iter.key()))){
                    diagnostic(Diag::Warn_Parser_Unused_PatternValue, iter.key(), iter.value());
                }
            }

            // because we remove used overwrites when they are referenced, overwriteValueNameToIndex should only contain unused ones
            if(Q_UNLIKELY(!overwriteValueNameToIndex.isEmpty())){
                for(auto iter = overwriteValueNameToIndex.begin(), iterEnd = overwriteValueNameToIndex.end(); iter != iterEnd; ++iter){
                    diagnostic(Diag::Warn_Parser_Unused_Overwrite, iter.key(), iter.value());
                }
            }
        } // Pattern

        // childNodeNameList from src / allowedChildNodeIndexList from dest is processed in second pass

        // earlyExitPatterns
        dest.earlyExitPatterns.reserve(src.earlyExitPatterns.size());
        for(int i = 0, n = src.earlyExitPatterns.size(); i < n; ++i){
            DiagnosticPathNode pathNode(diagnostic, tr("EarlyExitPattern %1").arg(i));
            dest.earlyExitPatterns.push_back(Parser::Pattern());
            Parser::Pattern& p = dest.earlyExitPatterns.back();
            QHash<QString,int> dummy;
            if(Q_UNLIKELY(!ptr->context.parsePatternString(src.earlyExitPatterns.at(i), p.elements, dummy, diagnostic))){
                isValidated = false;
                continue;
            }
        }

        // combineToIRNodeTypeName
        // if it is empty:
        //      set the dest combineToIRNodeTypeName to IRNode name if there is an IRNode with the same name
        //      otherwise leave it empty
        // if it is not empty:
        //      verify if the IRNode name exists
        if(!src.combineToNodeTypeName.isEmpty()){
            int irNodeIndex = ir.getNodeTypeIndex(src.combineToNodeTypeName);
            dest.combineToIRNodeIndex = irNodeIndex;
            if(Q_UNLIKELY(irNodeIndex == -1)){
                diagnostic(Diag::Error_Parser_BadReference_IRNodeName, src.combineToNodeTypeName);
                isValidated = false;
            }
        }else{
            dest.combineToIRNodeIndex = ir.getNodeTypeIndex(src.name);
        }

        // combinedNodeParams
        if(dest.combineToIRNodeIndex != -1){
            const IRNodeType& irNodeTy = ir.getNodeType(dest.combineToIRNodeIndex);
            int numParams = irNodeTy.getNumParameter();
            DiagnosticPathNode pathNode(diagnostic, tr("Conversion To IR Node"));
            pathNode.setDetailedName(irNodeTy.getName());
            if(src.combinedNodeParams.isEmpty()){
                // bind all parameters by name; ParserNode should contain parameter that IRNode expects
                dest.combineValueTransform.clear();
                for(int i = 0; i < numParams; ++i){
                    const QString& paramName = irNodeTy.getParameterName(i);
                    if(Q_UNLIKELY(!dest.paramName.contains(paramName))){
                        diagnostic(Diag::Error_Parser_BadConversionToIR_IRParamNotInitialized, paramName);
                        isValidated = false;
                    }
                }
            }else{
                // there are some overwrite entries
                dest.combineValueTransform.reserve(numParams);
                for(int i = 0; i < numParams; ++i){
                    dest.combineValueTransform.push_back(QList<QList<PatternValueSubExpression>>());
                }
                Q_ASSERT(dest.combineValueTransform.size() == numParams);
                for(auto iter = src.combinedNodeParams.begin(), iterEnd = src.combinedNodeParams.end();
                    iter != iterEnd; ++iter){
                    int paramIndex = irNodeTy.getParameterIndex(iter.key());
                    if(Q_UNLIKELY(paramIndex == -1)){
                        diagnostic(Diag::Error_Parser_BadConversionToIR_IRParamNotExist, iter.key());
                        isValidated = false;
                    }else{
                        // TODO
                        const QStringList& exprList = iter.value();
                        auto& destList = dest.combineValueTransform[paramIndex];
                        destList.reserve(exprList.size());
                        for(int i = 0, n = exprList.size(); i < n; ++i){
                            QSet<QString> referencedVals;
                            destList.push_back(QList<PatternValueSubExpression>());
                            if(Q_UNLIKELY(!ptr->context.parseValueTransformString(
                                              exprList.at(i),
                                              destList.back(),
                                              referencedVals,
                                              false,
                                              diagnostic))){
                                isValidated = false;
                            }
                            // we don't check reference made here yet
                        }
                    }
                } // finished handling all records in src.combinedNodeParams
            }
            // finished handling parameter conversion to IR node
        }
        Q_ASSERT(ptr->nodes.size() == i);
        ptr->nodes.push_back(dest);
    } // ParserNode

    // check if root node is good
    // although checking whether child name reference is good is done below, avoid doing here
    // would cause the entire tree to be unreachable and therefore will produce too many unhelpful warnings.
    int rootParserNodeIndex = nodeNameToIndex.value(policy.rootParserNodeName, -1);
    if(Q_UNLIKELY(rootParserNodeIndex == -1)){
        diagnostic(Diag::Error_Parser_BadRoot_BadReferenceByParserNodeName, policy.rootParserNodeName);
        isValidated = false;
    }else if(Q_UNLIKELY(ptr->nodes.at(rootParserNodeIndex).combineToIRNodeIndex == -1)){
        diagnostic(Diag::Error_Parser_BadRoot_NotConvertingToIR, policy.rootParserNodeName);
        isValidated = false;
    }

    if(!isValidated)
        return nullptr;

    // second pass: reorder parser nodes in BFS order
    Q_ASSERT(ptr->nodes.size() == policy.nodes.size());
    decltype(ptr->nodes) tmpNodes;
    tmpNodes.swap(ptr->nodes);
    ptr->nodes.reserve(tmpNodes.size());
    QQueue<int> rawNodeIndexQueue;
    std::size_t numNodes = static_cast<std::size_t>(ptr->nodes.size());
    // for each raw node index. what's the final index
    std::vector<int> rawNodeIndexToNewIndex(numNodes, -1);
    // used to check if there is any reference to a raw node index
    // also used to check if there is any duplicated reference from the same node
    // the value is the largest new node index making the reference
    // -2 for unreferenced node, -1 for root node, >=0 for nodes referenced by another node with that index
    std::vector<int> nodeReferenceChecker(numNodes, -2);
    rawNodeIndexQueue.enqueue(rootParserNodeIndex);
    nodeReferenceChecker.at(static_cast<std::size_t>(rootParserNodeIndex)) = -1;// root node is referenced
    while(!rawNodeIndexQueue.empty()){
        int rawIndex = rawNodeIndexQueue.dequeue();
        const Node& src = tmpNodes.at(rawIndex);
        int cookedIndex = ptr->nodes.size();
        rawNodeIndexToNewIndex.at(static_cast<std::size_t>(rawIndex))= cookedIndex;
        ptr->nodes.push_back(src);
        const ParserNode& srcNode = policy.nodes.at(rawIndex);
        Node& dest = ptr->nodes.back();
        // we will populate child node index here
        // use the raw index first, then use another pass to correct them afterwards
        dest.allowedChildNodeIndexList.reserve(srcNode.childNodeNameList.size());
        for(int i = 0, n = srcNode.childNodeNameList.size(); i < n; ++i){
            const QString& childName = srcNode.childNodeNameList.at(i);
            int childRawIndex = nodeNameToIndex.value(childName, -1);
            if(Q_UNLIKELY(childRawIndex == -1)){
                diagnostic(Diag::Error_Parser_BadTree_BadChildNodeReference, src.nodeName, childName);
                isValidated = false;
            }else{
                std::size_t castedChildRawIndex = static_cast<std::size_t>(childRawIndex);
                int oldval = nodeReferenceChecker.at(castedChildRawIndex);
                nodeReferenceChecker.at(castedChildRawIndex) = rawIndex;

                if(Q_UNLIKELY(oldval == rawIndex)){
                    // duplicated reference
                    diagnostic(Diag::Warn_Parser_DuplicatedReference_ChildParserNode, src.nodeName, childName);
                }else{
                    dest.allowedChildNodeIndexList.push_back(childRawIndex);
                }

                // add to queue if this node is not referenced before
                if(oldval == -2){
                    rawNodeIndexQueue.enqueue(childRawIndex);
                }
            }
        }
    }

    // warning on all unreferenced nodes
    if(Q_UNLIKELY(ptr->nodes.size() != policy.nodes.size())){
        int missingCount = 0;
        for(std::size_t i = 0; i < numNodes; ++i){
            // consistency check
            Q_ASSERT((nodeReferenceChecker.at(i) == -2) == (rawNodeIndexToNewIndex.at(i) == -1));
            if(nodeReferenceChecker.at(i) == -2){
                missingCount += 1;
                diagnostic(Diag::Warn_Parser_UnreachableNode, policy.nodes.at(static_cast<int>(i)).name);
            }
        }
        Q_ASSERT(missingCount + ptr->nodes.size() == policy.nodes.size());
    }

    // update all indices to the new one
    for(auto& node : ptr->nodes){
        for(int& childIndex : node.allowedChildNodeIndexList){
            childIndex = rawNodeIndexToNewIndex.at(static_cast<std::size_t>(childIndex));
            Q_ASSERT(childIndex >= 0);
        }
    }

    // done!
    return ptr.release();
}

int Parser::computePatternScore(const QList<SubPattern>& pattern, const QList<int>& matchPairScore)
{
    // score computation rule:
    // if we have a literal: score is incremented by 2 * (length of literal)
    // if we have a match pair, then the score is incremented by the score from matchPairScore
    // (which should be (minimum length of match pair start + minimum length of match pair end))
    // if we have an Auto, the score is incremented by 1
    // if we have a regex, the score is incremented by length of regex string
    int score = 0;
    for(const SubPattern& p : pattern){
        switch(p.ty){
        case SubPatternType::Literal:{
            score += p.literalData.str.length() * 2;
        }break;
        case SubPatternType::MatchPair:{
            score += matchPairScore.at(p.matchPairData.matchPairIndex);
        }break;
        case SubPatternType::Regex:{
            score += p.regexData.regex.pattern().length();
        }break;
        case SubPatternType::Auto:{
            score += 1;
        }break;
        }
    }
    return score;
}

bool Parser::ParseContext::parsePatternString(
        const QString& pattern,
        QList<SubPattern> &result,
        QHash<QString, int> &valueNameToIndex,
        DiagnosticEmitterBase& diagnostic)
{
    QStringRef view(&pattern);

    // keep a copy of first patterns we put
    int startIndex = result.size();

    struct MatchPairFrame{
        int index;
        QStringRef startMarkRef; // used for error reporting
    };

    QList<MatchPairFrame> matchPairStack;// used to report error if the pattern don't have matching match pairs

    // we will do some fixup if this is true
    QStringRef lastAutoSubPatternStr;
    bool isLastPatternAutoNeedFixup = false;
    while(!view.isEmpty()){

        // ignore strings from ignoreList first
        int ignoredStrLength = removeLeadingIgnoredString(view);
        if(ignoredStrLength != 0){
            if(isLastPatternAutoNeedFixup){
                //an auto pattern followed by ignore string
                Q_ASSERT(!result.isEmpty());
                auto& p = result.back();
                Q_ASSERT(p.ty == SubPatternType::Auto);
                p.autoData.isTerminateByIgnoredString = true;
                if(Q_UNLIKELY(p.autoData.nextSubPatternIncludeLength != 0)){
                    StringDiagnosticRecord d;
                    d.str = pattern;
                    // info interval is the ignored string
                    d.infoStart = view.position() - ignoredStrLength;
                    d.infoEnd = view.position();
                    // error interval is the auto mattern
                    d.errorStart = lastAutoSubPatternStr.position();
                    d.errorEnd = lastAutoSubPatternStr.position() + lastAutoSubPatternStr.length();
                    diagnostic(Diag::Error_Parser_BadPattern_Expr_InvalidNextPatternForInclusion, d);
                    return false;
                }
            }
        }
        isLastPatternAutoNeedFixup = false;

        if(view.isEmpty())
            break;

        if(view.startsWith(exprStartMark)){
            // assume exprStartMark = "<" and exprEndMark = ">"
            // regex: something like <[regex]"**(...)**">
            // literal: something like <"**(...)**">
            // UpToTerminator: anything left
            QStringRef bodyStart = view.mid(exprStartMark.length());
            QStringRef engineText;
            QStringRef body;
            bool isEngineSpecified = false;
            bool isReferenceExpr = false;
            if(bodyStart.startsWith('[')){
                int endIndex = bodyStart.indexOf(']',1);
                if(Q_UNLIKELY(endIndex == -1)){
                    StringDiagnosticRecord d;
                    d.str = pattern;
                    // info interval is everything after exprStartMark
                    d.infoStart = view.position();
                    d.infoEnd = pattern.length();
                    // error interval is '['
                    d.errorStart = bodyStart.position();
                    d.errorEnd = bodyStart.position()+1;
                    diagnostic(Diag::Error_Parser_BadPattern_Expr_MissingEngineNameEndMark, d);
                    return false;
                }
                engineText = bodyStart.mid(1,endIndex-1);
                bodyStart = bodyStart.mid(endIndex+1);
                isEngineSpecified = true;
            }
            if(Q_UNLIKELY(bodyStart.isEmpty())){
                StringDiagnosticRecord d;
                d.str = pattern;
                // info interval is everything after exprStartMark
                d.infoStart = view.position();
                d.infoEnd = pattern.length();
                // error interval is exprStartMark
                d.errorStart = view.position();
                d.errorEnd = view.position()+1;
                diagnostic(Diag::Error_Parser_BadPattern_Expr_ExpectingExpressionContent, d);
                return false;
            }
            bool isRawStringLiteral = bodyStart.startsWith('(');
            bool isDirectQuotedStringLiteral = bodyStart.startsWith('"');
            if(isRawStringLiteral || isDirectQuotedStringLiteral){
                int tailStartIndex = -1;
                if(isRawStringLiteral){
                    // raw string literal style
                    int startQuoteIndex = bodyStart.indexOf('"', 1);
                    if(Q_UNLIKELY(startQuoteIndex == -1)){
                        StringDiagnosticRecord d;
                        d.str = pattern;
                        // info interval is everything after exprStartMark
                        d.infoStart = view.position();
                        d.infoEnd = pattern.length();
                        // error interval is '('
                        d.errorStart = bodyStart.position();
                        d.errorEnd = bodyStart.position()+1;
                        diagnostic(Diag::Error_Parser_BadPattern_Expr_RawStringMissingQuoteStart, d);
                        return false;
                    }
                    QStringRef delimitor = bodyStart.mid(1, startQuoteIndex-1);
                    QStringRef quotedContentStart = bodyStart.mid(startQuoteIndex+1);
                    QString expectedTerminator;
                    Q_ASSERT(delimitor.length() == startQuoteIndex-1);
                    expectedTerminator.reserve(startQuoteIndex+1);
                    expectedTerminator.push_back('"');
                    expectedTerminator.append(delimitor);
                    expectedTerminator.push_back(')');
                    int contentLength = quotedContentStart.indexOf(expectedTerminator);
                    if(Q_UNLIKELY(contentLength == -1)){
                        StringDiagnosticRecord d;
                        d.str = pattern;
                        // info interval is everything after exprStartMark
                        d.infoStart = view.position();
                        d.infoEnd = pattern.length();
                        // error interval is the delimitor with '(' and '"'
                        d.errorStart = bodyStart.position();
                        d.errorEnd = bodyStart.position() + startQuoteIndex + 1;
                        diagnostic(Diag::Error_Parser_BadPattern_Expr_UnterminatedQuote, d);
                        return false;
                    }
                    body = quotedContentStart.left(contentLength);
                    tailStartIndex = 2*(startQuoteIndex+1) + contentLength;
                }else{
                    // direct quote; use '"' as terminator
                    int bodyEndIndex = bodyStart.indexOf('"', 1);
                    if(Q_UNLIKELY(bodyEndIndex == -1)){
                        StringDiagnosticRecord d;
                        d.str = pattern;
                        // info interval is everything after exprStartMark
                        d.infoStart = view.position();
                        d.infoEnd = pattern.length();
                        // error interval is '"'
                        d.errorStart = bodyStart.position();
                        d.errorEnd = bodyStart.position()+1;
                        diagnostic(Diag::Error_Parser_BadPattern_Expr_UnterminatedQuote, d);
                        return false;
                    }
                    body = bodyStart.mid(1, bodyEndIndex-1);
                    tailStartIndex = bodyEndIndex + 1;
                }
                // make sure the string is not empty
                if(Q_UNLIKELY(body.isEmpty())){
                    StringDiagnosticRecord d;
                    d.str = pattern;
                    // info interval is from the exprStartMark to end of quote
                    d.infoStart = view.position();
                    d.infoEnd = bodyStart.position() + tailStartIndex;
                    // error interval is the quote
                    d.errorStart = bodyStart.position();
                    d.errorEnd = bodyStart.position() + tailStartIndex;
                    diagnostic(Diag::Error_Parser_BadPattern_Expr_EmptyBody, d);
                    return false;
                }
                // make sure there is no garbage before the end mark and after the string
                QStringRef tail = bodyStart.mid(tailStartIndex);
                int endMarkIndex = tail.indexOf(exprEndMark);
                if(Q_UNLIKELY(endMarkIndex == -1)){
                    // no end marker
                    StringDiagnosticRecord d;
                    d.str = pattern;
                    // info interval is everything after exprStartMark
                    d.infoStart = view.position();
                    d.infoEnd = pattern.length();
                    // error interval is everything starting from the tail
                    d.errorStart = tail.position();
                    d.errorEnd = pattern.length();
                    diagnostic(Diag::Error_Parser_BadPattern_Expr_UnterminatedExpr, d);
                    return false;
                }else if(Q_UNLIKELY(endMarkIndex != 0)){
                    StringDiagnosticRecord d;
                    d.str = pattern;
                    // info interval is the entire expr
                    d.infoStart = view.position();
                    d.infoEnd = tail.position() + endMarkIndex + exprEndMark.length();
                    // error interval is the garbage at end
                    d.errorStart = tail.position();
                    d.errorEnd = tail.position() + endMarkIndex;
                    diagnostic(Diag::Error_Parser_BadPattern_Expr_GarbageAtEnd, d);
                    return false;
                }

                // the tail looks good
                // start to validate this expression
                SubPattern expr;
                int subPatternIndex = result.size();
                if(isEngineSpecified){
                    if(engineText == "regex"){
                        expr.ty = SubPatternType::Regex;
                        expr.regexData.regex.setPattern(body.toString());
                        if(Q_UNLIKELY(!expr.regexData.regex.isValid())){
                            StringDiagnosticRecord d;
                            d.str = pattern;
                            // info interval is the regular expression string
                            d.infoStart = body.position();
                            d.infoEnd = body.position() + body.length();
                            // error interval is the character marked by patternErrorOffset
                            d.errorStart = body.position() + expr.regexData.regex.patternErrorOffset();
                            d.errorEnd = d.errorStart + 1;
                            diagnostic(Diag::Error_Parser_BadPattern_Expr_BadRegex, d, expr.regexData.regex.errorString());
                            return false;
                        }
                        // fill in all named captures made by this regex
                        for(const QString& capture : expr.regexData.regex.namedCaptureGroups()){
                            if(capture.isEmpty())
                                continue;
                            auto iter = valueNameToIndex.find(capture);
                            if(Q_UNLIKELY(iter != valueNameToIndex.end())){
                                diagnostic(Diag::Error_Parser_BadPattern_Expr_DuplicatedDefinition, capture, iter.value(), subPatternIndex);
                                return false;
                            }
                            valueNameToIndex.insert(capture, subPatternIndex);
                        }
                    }else{
                        StringDiagnosticRecord d;
                        d.str = pattern;
                        // info interval is the entire expr
                        d.infoStart = view.position();
                        d.infoEnd = tail.position() + endMarkIndex + exprEndMark.length();
                        // error interval is the engine specifier
                        d.errorStart = engineText.position() - 1;
                        d.errorEnd = engineText.position() + engineText.length() + 1;
                        diagnostic(Diag::Error_Parser_BadPattern_Expr_UnrecognizedEngine, d);
                        return false;
                    }
                }else{
                    // literal string
                    expr.ty = SubPatternType::Literal;
                    expr.literalData.str = body.toString();
                }
                // we successfully consumed the expression
                view = tail.mid(exprEndMark.length());
                result.push_back(expr);
            }else{
                // no string literal find; probably a reference expression
                if(isEngineSpecified){
                    // if there is an engine specifier, we must use a string literal after it
                    StringDiagnosticRecord d;
                    d.str = pattern;
                    // info interval is the engine text with []
                    d.infoStart = engineText.position()-1;
                    d.infoEnd = engineText.position() + engineText.length() + 1;
                    // error interval is the character after ']'
                    // we know there is a character there, since we check bodyStart.isEmpty() first
                    d.errorStart = d.infoEnd;
                    d.errorEnd = d.infoEnd + 1;
                    diagnostic(Diag::Error_Parser_BadPattern_Expr_NoRawLiteralAfterEngineSpecifier, d);
                    return false;
                }
                isReferenceExpr = true;
                // direct search on exprEndMark
                int endMarkIndex = bodyStart.indexOf(exprEndMark);
                if(Q_UNLIKELY(endMarkIndex == -1)){
                    StringDiagnosticRecord d;
                    d.str = pattern;
                    // info interval is everything after exprStartMark
                    d.infoStart = view.position();
                    d.infoEnd = pattern.length();
                    // error interval is the start mark
                    d.errorStart = view.position();
                    d.errorEnd = bodyStart.position();
                    diagnostic(Diag::Error_Parser_BadPattern_Expr_UnterminatedExpr, d);
                    return false;
                }
                QStringRef exprFullString = view.left(exprStartMark.length() + endMarkIndex + exprEndMark.length()); // just for error messages
                QStringRef referencedName = bodyStart.left(endMarkIndex);
                bool isIncludeSuccessiveTerminator = referencedName.endsWith('*');
                if(isIncludeSuccessiveTerminator){
                    referencedName.chop(1);
                }
                bool isIncludeTerminator = referencedName.endsWith('+');
                if(isIncludeTerminator){
                    referencedName.chop(1);
                }
                // we require +* at the end for including all successive terminator
                if(Q_UNLIKELY(isIncludeSuccessiveTerminator && !isIncludeTerminator)){
                    StringDiagnosticRecord d;
                    d.str = pattern;
                    // info interval is the entire expression
                    d.infoStart = exprFullString.position();
                    d.infoEnd = exprFullString.position() + exprFullString.length();
                    // error interval is the terminator Inclusion specifier
                    d.errorStart = referencedName.position() + referencedName.length();
                    d.errorEnd = d.errorStart + (isIncludeSuccessiveTerminator? 1:0) + (isIncludeTerminator? 1:0); // basically errorStart + 1
                    diagnostic(Diag::Error_Parser_BadPattern_Expr_BadTerminatorInclusionSpecifier, d);
                    return false;
                }

                // check if the name is good
                QString finalName = referencedName.toString();
                // the name can be empty, if the match result is not used
                if(!finalName.isEmpty()){
                    if(Q_UNLIKELY(!IRNodeType::validateName(diagnostic, finalName))){
                        StringDiagnosticRecord d;
                        d.str = pattern;
                        // info interval is the entire expression
                        d.infoStart = exprFullString.position();
                        d.infoEnd = exprFullString.position() + exprFullString.length();
                        // error interval is the name reference
                        d.errorStart = referencedName.position();
                        d.errorEnd = referencedName.position() + referencedName.length();
                        diagnostic(Diag::Error_Parser_BadPattern_Expr_BadNameForReference, d);
                        return false;
                    }
                }
                SubPattern expr;
                expr.ty = SubPatternType::Auto;
                expr.autoData.valueName = finalName;
                expr.autoData.isTerminateByIgnoredString = false;// we will correct this in second pass
                expr.autoData.nextSubPatternIncludeLength = (isIncludeTerminator? (isIncludeSuccessiveTerminator? -1: 1): 0);
                // if the last sub pattern is also an auto sub pattern, make it terminate by ignored string
                if(!result.isEmpty()){
                    auto& p = result.back();
                    if(p.ty == SubPatternType::Auto){
                        p.autoData.isTerminateByIgnoredString = true;
                        if(Q_UNLIKELY(p.autoData.nextSubPatternIncludeLength != 0)){
                            StringDiagnosticRecord d;
                            d.str = pattern;
                            // info interval is the current sub pattern
                            d.infoStart = exprFullString.position();
                            d.infoEnd = exprFullString.position() + exprFullString.length();
                            // error interval is the auto mattern
                            d.errorStart = lastAutoSubPatternStr.position();
                            d.errorEnd = lastAutoSubPatternStr.position() + lastAutoSubPatternStr.length();
                            diagnostic(Diag::Error_Parser_BadPattern_Expr_InvalidNextPatternForInclusion, d);
                            return false;
                        }
                    }
                }
                // we successfully consumed the expression
                isLastPatternAutoNeedFixup = true;
                lastAutoSubPatternStr = exprFullString;
                view = view.mid(exprStartMark.length() + endMarkIndex + exprEndMark.length());
                result.push_back(expr);
            }
            // done processing cases with exprStartMark
        }else{
            // start of a literal, or a match pair marker
            bool isMatchPairFound = false;

            if(!matchPairStack.isEmpty()){
                // check if it is the matching end
                int index = matchPairStack.back().index;
                int maxLen = 0;
                for(const QString& endMark : matchPairEnds.at(index)){
                    if(view.startsWith(endMark)){
                        if(maxLen < endMark.length()){
                            maxLen = endMark.length();
                        }
                    }
                }
                if(maxLen > 0){
                    matchPairStack.pop_back();
                    view = view.mid(maxLen);
                    SubPattern expr;
                    expr.ty = SubPatternType::MatchPair;
                    expr.matchPairData.matchPairIndex = index;
                    expr.matchPairData.isStart = false;
                    result.push_back(expr);
                    isMatchPairFound = true;
                }
            }

            if(!isMatchPairFound){
                // check if we are starting a scope
                int maxLen = 0;
                int matchPairIndex = -1;
                for(int i = 0, n = matchPairStarts.size(); i < n; ++i){
                    for(const QString& startMark : matchPairStarts.at(i)){
                        if(view.startsWith(startMark)){
                            if(maxLen < startMark.length()){
                                maxLen = startMark.length();
                                matchPairIndex = i;
                            }
                        }
                    }
                }
                if(matchPairIndex > 0){
                    MatchPairFrame f;
                    f.index = matchPairIndex;
                    f.startMarkRef = view.left(maxLen);
                    matchPairStack.push_back(f);
                    view = view.mid(maxLen);
                    SubPattern expr;
                    expr.ty = SubPatternType::MatchPair;
                    expr.matchPairData.matchPairIndex = matchPairIndex;
                    expr.matchPairData.isStart = true;
                    result.push_back(expr);
                    isMatchPairFound = true;
                }else{
                    // check if this is a match pair end without start mark
                    // if yes then report an error
                    int endMaxLen = 0;
                    for(int i = 0, n = matchPairEnds.size(); i < n; ++i){
                        for(const QString& endMark : matchPairEnds.at(i)){
                            if(Q_UNLIKELY(view.startsWith(endMark))){
                                if(endMaxLen < endMark.length()){
                                    endMaxLen = endMark.length();
                                }
                            }
                        }
                    }
                    if(Q_UNLIKELY(endMaxLen > 0)){
                        QStringRef endMarkRef = view.left(endMaxLen);
                        StringDiagnosticRecord d;
                        d.str = pattern;
                        // both interval is the end marker string
                        d.errorStart = d.infoStart = endMarkRef.position();
                        d.errorEnd   = d.infoEnd   = endMarkRef.position() + endMarkRef.length();
                        diagnostic(Diag::Error_Parser_BadPattern_Expr_UnexpectedMatchPairEnd, d);
                        return false;
                    }
                    // now we confirm that we are starting a literal
                    // keep extracting characters until a start indicator appears
                    QStringRef literalStart = view;
                    while(true){
                        view = view.mid(1);

                        if(view.isEmpty())
                            break;

                        int removedLen = removeLeadingIgnoredString(view);

                        // finding a string from ignored list terminates current implicit string literal
                        if(removedLen > 0)
                            break;

                        if(view.startsWith(exprStartMark))
                            break;

                        bool isMatchPairFound = false;
                        for(int i = 0, n = matchPairStarts.size(); i < n; ++i){
                            for(const QString& mark : matchPairStarts.at(i)){
                                if(view.startsWith(mark)){
                                    isMatchPairFound = true;
                                    break;
                                }
                            }
                            if(isMatchPairFound)
                                break;
                        }
                        if(isMatchPairFound)
                            break;
                        for(int i = 0, n = matchPairEnds.size(); i < n; ++i){
                            for(const QString& mark : matchPairEnds.at(i)){
                                if(view.startsWith(mark)){
                                    isMatchPairFound = true;
                                    break;
                                }
                            }
                            if(isMatchPairFound)
                                break;
                        }
                        if(isMatchPairFound)
                            break;
                    }
                    SubPattern expr;
                    expr.ty = SubPatternType::Literal;
                    expr.literalData.str = literalStart.chopped(view.length()).toString();
                    result.push_back(expr);
                }
                // done cases not ending a match pair enclosure
            }
            // done for match pair and literals
        }
    }

    // report error if we don't have matching match pairs
    if(Q_UNLIKELY(!matchPairStack.isEmpty())){
        StringDiagnosticRecord d;
        d.str = pattern;
        for(int i = 0, n = matchPairStack.size(); i < n; ++i){
            const auto& f = matchPairStack.at(i);
            d.errorStart = d.infoStart = f.startMarkRef.position();
            d.errorEnd   = d.infoEnd   = f.startMarkRef.position() + f.startMarkRef.length();
            diagnostic(Diag::Error_Parser_BadPattern_UnmatchedMatchPairStart, d);
        }
        return false;
    }

    // no empty pattern accepted
    if(Q_UNLIKELY(result.size() == startIndex)){
        diagnostic(Diag::Error_Parser_BadPattern_EmptyPattern);
        return false;
    }

    // correct the last Auto type sub patterns
    {
        auto& p = result.back();
        if(p.ty == SubPatternType::Auto){
            // make sure we do not set inclusion flag
            if(Q_UNLIKELY(p.autoData.nextSubPatternIncludeLength != 0)){
                StringDiagnosticRecord d;
                d.str = pattern;
                // info interval is everything after the auto sub pattern
                d.infoStart = lastAutoSubPatternStr.position() + lastAutoSubPatternStr.length();
                d.infoEnd = pattern.length();
                // error interval is the auto mattern
                d.errorStart = lastAutoSubPatternStr.position();
                d.errorEnd = lastAutoSubPatternStr.position() + lastAutoSubPatternStr.length();
                diagnostic(Diag::Error_Parser_BadPattern_Expr_InvalidNextPatternForInclusion, d);
                return false;
            }
            p.autoData.isTerminateByIgnoredString = true;
        }
    }

    return true;
}

bool Parser::ParseContext::parseValueTransformString(const QString& transform,
        QList<PatternValueSubExpression> &result,
        QSet<QString> &referencedValues,
        bool isLocalOnly,
        DiagnosticEmitterBase &diagnostic)
{
    auto helper_getEnclosedLiteral = [&](QStringRef text, QStringRef& result, int faultInfoStartOffset)->int{
        bool isRawStringLiteral = text.startsWith('(');
        bool isDirectQuotedStringLiteral = text.startsWith('"');
        if(Q_UNLIKELY(!isRawStringLiteral && !isDirectQuotedStringLiteral)){
            // nothing in expectation
            StringDiagnosticRecord d;
            d.str = transform;
            // info interval is from the offset given by faultInfoStartOffset to first character
            d.infoStart = faultInfoStartOffset + text.position();
            d.infoEnd = text.position() + 1;
            // error interval is the first character of text
            d.errorStart = text.position();
            d.errorEnd = text.position() + 1;
            diagnostic(Diag::Error_Parser_BadValueTransform_ExpectingLiteralExpr, d);
            return -1;
        }
        int nextDoubleQuoteIndex = text.indexOf('"',1);
        if(isDirectQuotedStringLiteral){
            if(Q_UNLIKELY(nextDoubleQuoteIndex == -1)){
                // unterminated quote
                StringDiagnosticRecord d;
                d.str = transform;
                // info interval is from the offset given by faultInfoStartOffset to first character
                d.infoStart = faultInfoStartOffset + text.position();
                d.infoEnd = text.position() + 1;
                // error interval is the first character of text
                d.errorStart = text.position();
                d.errorEnd = text.position() + 1;
                diagnostic(Diag::Error_Parser_BadValueTransform_UnterminatedQuote, d);
                return -1;
            }
            result = text.mid(1).left(nextDoubleQuoteIndex-1);
            return nextDoubleQuoteIndex+1;
        }
        // raw string literal style quote
        if(Q_UNLIKELY(nextDoubleQuoteIndex == -1)){
            // raw string missing starting quote
            StringDiagnosticRecord d;
            d.str = transform;
            // info interval is from the offset given by faultInfoStartOffset to first character
            d.infoStart = faultInfoStartOffset + text.position();
            d.infoEnd = text.position() + 1;
            // error interval is the first character of text
            d.errorStart = text.position();
            d.errorEnd = text.position() + 1;
            diagnostic(Diag::Error_Parser_BadValueTransform_RawStringMissingQuoteStart, d);
            return -1;
        }
        QStringRef rawStringStartMark = text.left(nextDoubleQuoteIndex+1);
        QString rawStringEndMark;
        rawStringEndMark.reserve(rawStringStartMark.size());
        rawStringEndMark.append('"');
        rawStringEndMark.append(rawStringStartMark.mid(1).chopped(1));
        rawStringEndMark.append(')');
        int endIndex = text.indexOf(rawStringEndMark, nextDoubleQuoteIndex+1);
        if(Q_UNLIKELY(endIndex == -1)){
            // unterminated quote
            StringDiagnosticRecord d;
            d.str = transform;
            // info interval is from the offset given by faultInfoStartOffset to end of rawStringStartMark
            d.infoStart = faultInfoStartOffset + text.position();
            d.infoEnd = rawStringStartMark.position() + rawStringStartMark.length();
            // error interval is rawStringStartMark
            d.errorStart = rawStringStartMark.position();
            d.errorEnd = rawStringStartMark.position() + rawStringStartMark.length();
            diagnostic(Diag::Error_Parser_BadValueTransform_UnterminatedQuote, d);
            return -1;
        }
        result = text.left(endIndex).mid(rawStringStartMark.length());
        return endIndex + rawStringEndMark.length();
    };

    QStringRef text(&transform);
    while(!text.isEmpty()){
        int index = text.indexOf(exprStartMark);
        if(index == -1){
            // no more special patterns to handle
            PatternValueSubExpression expr;
            expr.ty = PatternValueSubExpression::OpType::Literal;
            expr.literalData.str = text.toString();
            result.push_back(expr);
            return true;
        }

        if(index != 0){
            QStringRef literal = text.left(index);
            // we will start a special part
            // add this literal to output first
            PatternValueSubExpression expr;
            expr.ty = PatternValueSubExpression::OpType::Literal;
            expr.literalData.str = literal.toString();
            result.push_back(expr);
            text = text.mid(index);
        }

        // now we need to deal with a special block
        // skip the start mark first
        Q_ASSERT(text.startsWith(exprStartMark));
        QStringRef bodyStart = text.mid(exprStartMark.length());
        bool isRawStringLiteral = bodyStart.startsWith('(');
        bool isDirectQuotedStringLiteral = bodyStart.startsWith('"');
        if(isRawStringLiteral || isDirectQuotedStringLiteral){
            QStringRef literal;
            int advanceDist = helper_getEnclosedLiteral(bodyStart, literal, -exprStartMark.length());
            if(advanceDist == -1){
                return false;
            }
            QStringRef textAfterString = bodyStart.mid(advanceDist);
            if(Q_UNLIKELY(textAfterString.isEmpty())){
                // unterminated expr
                StringDiagnosticRecord d;
                d.str = transform;
                // info interval is from start mark to position of textAfterString
                d.infoStart = text.position();
                d.infoEnd = textAfterString.position();
                // error interval is the start mark
                d.errorStart = text.position();
                d.errorEnd = text.position() + exprStartMark.length();
                diagnostic(Diag::Error_Parser_BadValueTransform_UnterminatedExpr, d);
                return false;
            }else if(Q_UNLIKELY(!textAfterString.startsWith(exprEndMark))){
                // garbage at end
                StringDiagnosticRecord d;
                d.str = transform;
                // info interval is from start mark to position of textAfterString
                d.infoStart = text.position();
                d.infoEnd = textAfterString.position();
                // error interval is the first character of textAfterString
                d.errorStart = textAfterString.position();
                d.errorEnd = textAfterString.position()+1;
                diagnostic(Diag::Error_Parser_BadValueTransform_GarbageAtExprEnd, d);
                return false;
            }
            PatternValueSubExpression expr;
            expr.ty = PatternValueSubExpression::OpType::Literal;
            expr.literalData.str = literal.toString();
            result.push_back(expr);
            text = textAfterString.mid(exprEndMark.length());
        }else{
            // direct search for end mark, speculatively for extern reference
            int endMarkIndex = bodyStart.indexOf(exprEndMark);
            if(Q_UNLIKELY(endMarkIndex == -1)){
                StringDiagnosticRecord d;
                d.str = transform;
                // info interval is everything after start mark
                d.infoStart = text.position();
                d.infoEnd = transform.length();
                // error interval is exprStartMark
                d.errorStart = text.position();
                d.errorEnd = bodyStart.position();
                diagnostic(Diag::Error_Parser_BadValueTransform_UnterminatedExpr, d);
                return false;
            }
            QStringRef refVal = bodyStart.left(endMarkIndex);
            bool isLocalReference = refVal.contains('.');
            if(Q_UNLIKELY(!isLocalReference && isLocalOnly)){
                StringDiagnosticRecord d;
                d.str = transform;
                // info interval is everything inside expr
                d.infoStart = text.position();
                d.infoEnd = bodyStart.position() + endMarkIndex + exprEndMark.length();
                // error interval is refVal
                d.errorStart = refVal.position();
                d.errorEnd = refVal.position() + refVal.length();
                diagnostic(Diag::Error_Parser_BadValueTransform_NonLocalAccessInLocalOnlyEnv, d);
                return false;
            }
            PatternValueSubExpression expr;
            if(isLocalReference){
                expr.ty = PatternValueSubExpression::OpType::LocalReference;
                expr.localReferenceData.valueName = refVal.toString();
                if(Q_UNLIKELY(!IRNodeType::validateName(diagnostic, expr.localReferenceData.valueName))){
                    StringDiagnosticRecord d;
                    d.str = transform;
                    // info interval is everything inside expr
                    d.infoStart = text.position();
                    d.infoEnd = bodyStart.position() + endMarkIndex + exprEndMark.length();
                    // error interval is refVal
                    d.errorStart = refVal.position();
                    d.errorEnd = refVal.position() + refVal.length();
                    diagnostic(Diag::Error_Parser_BadValueTransform_InvalidNameForReference, d);
                    return false;
                }
                referencedValues.insert(expr.localReferenceData.valueName);
                text = bodyStart.mid(endMarkIndex + exprEndMark.length());
            }else{
                expr.ty = PatternValueSubExpression::OpType::ExternReference;
                // because we allow string literals in search expression, the endMarkIndex can be invalid
                // therefore for ExternReference we need to go step by step; parse all node traversal steps then value name
                QList<PatternValueSubExpression::ExternReferenceData::NodeTraverseStep>& stepResult = expr.externReferenceData.nodeTraversal;
                QStringRef traverseDistLeft = bodyStart;
                if(traverseDistLeft.startsWith('/')){
                    expr.externReferenceData.isTraverseStartFromRoot = true;
                    traverseDistLeft = traverseDistLeft.mid(1);
                }
                if(Q_UNLIKELY(traverseDistLeft.isEmpty())){
                    StringDiagnosticRecord d;
                    d.str = transform;
                    // info interval is bodyStart to current position
                    d.infoStart = bodyStart.position();
                    d.infoEnd = traverseDistLeft.position();
                    // error interval is the last character we just consumed
                    d.errorStart = traverseDistLeft.position() - 1;
                    d.errorEnd = traverseDistLeft.position();
                    diagnostic(Diag::Error_Parser_BadValueTransform_ExpectTraverseExpr, d);
                    return false;
                }
                while(true){
                    Q_ASSERT(!traverseDistLeft.isEmpty());
                    int stepStart = traverseDistLeft.position(); // for error reporting
                    PatternValueSubExpression::ExternReferenceData::NodeTraverseStep s;
                    if(traverseDistLeft.startsWith('/')){
                        traverseDistLeft = traverseDistLeft.mid(1);
                        continue;
                    }
                    if(traverseDistLeft.startsWith("./")){
                        traverseDistLeft = traverseDistLeft.mid(2);
                        continue;
                    }
                    if(traverseDistLeft.startsWith("../")){
                        s.ty = PatternValueSubExpression::ExternReferenceData::NodeTraverseStep::StepType::Parent;
                        stepResult.push_back(s);
                        traverseDistLeft = traverseDistLeft.mid(3);
                        continue;
                    }
                    // everything else is for going to child
                    int openSquareBracketIndex = traverseDistLeft.indexOf('[');
                    if(Q_UNLIKELY(openSquareBracketIndex == -1)){
                        StringDiagnosticRecord d;
                        d.str = transform;
                        // info interval is everything from bodyStart to current position
                        d.infoStart = bodyStart.position();
                        d.infoEnd = bodyStart.position()+ bodyStart.length();
                        // error interval is everything in traverseDistLeft (basically this step)
                        d.errorStart = traverseDistLeft.position();
                        d.errorEnd = traverseDistLeft.position() + traverseDistLeft.length();
                        diagnostic(Diag::Error_Parser_BadValueTransform_MissingChildSearchExpr, d);
                        return false;
                    }
                    bool isChildNameProvided = (openSquareBracketIndex > 0);
                    if(isChildNameProvided){
                        // we have a child name field
                        QStringRef childName = traverseDistLeft.left(openSquareBracketIndex);
                        s.childParserNodeName = childName.toString();
                        if(Q_UNLIKELY(!IRNodeType::validateName(diagnostic, s.childParserNodeName))){
                            StringDiagnosticRecord d;
                            d.str = transform;
                            // info interval is everything from bodyStart to current position
                            d.infoStart = bodyStart.position();
                            d.infoEnd = childName.position() + childName.length();
                            // error interval is the child node name
                            d.errorStart = childName.position();
                            d.errorEnd = childName.position() + childName.length();
                            diagnostic(Diag::Error_Parser_BadValueTransform_InvalidNameForReference, d);
                            return false;
                        }
                        traverseDistLeft = traverseDistLeft.mid(openSquareBracketIndex);
                    }
                    traverseDistLeft = traverseDistLeft.mid(1); // skip '['
                    int closeSquareBracketIndex = traverseDistLeft.indexOf(']');
                    if(Q_UNLIKELY(closeSquareBracketIndex == -1)){
                        StringDiagnosticRecord d;
                        d.str = transform;
                        // info interval is everything from bodyStart to current position
                        d.infoStart = bodyStart.position();
                        d.infoEnd = traverseDistLeft.position();
                        // error interval is the '['
                        d.errorStart = stepStart + openSquareBracketIndex;
                        d.errorEnd = stepStart + openSquareBracketIndex + 1;
                        Q_ASSERT(d.infoEnd == d.errorEnd);
                        diagnostic(Diag::Error_Parser_BadValueTransform_UnterminatedChildSearchExpr, d);
                        return false;
                    }
                    // no matter whether we have a literal with ']' included, if we have a key based search,
                    // the "==" string must appear before ']' because the only thing before "==" is the key parameter name
                    QStringRef possibleSearchExprEnclosure = traverseDistLeft.left(closeSquareBracketIndex);
                    int keyEndIndex = possibleSearchExprEnclosure.indexOf("==");
                    if(keyEndIndex == -1){
                        // number (index/offset) based search
                        if(isChildNameProvided){
                            s.ty = PatternValueSubExpression::ExternReferenceData::NodeTraverseStep::StepType::ChildByTypeAndOrder;
                        }else{
                            s.ty = PatternValueSubExpression::ExternReferenceData::NodeTraverseStep::StepType::AnyChildByOrder;
                        }
                        QStringRef num = possibleSearchExprEnclosure;
                        s.ioSearchData.isNumIndexInsteadofOffset = !(num.startsWith('+') || num.startsWith('-'));
                        bool isNumberGood = false;
                        s.ioSearchData.lookupNum = num.toInt(&isNumberGood);
                        if(Q_UNLIKELY(!isNumberGood)){
                            StringDiagnosticRecord d;
                            d.str = transform;
                            // info interval is everything from stepStart to current position
                            d.infoStart = stepStart;
                            d.infoEnd = num.position() + num.length();
                            // error interval is the number expression
                            d.errorStart = num.position();
                            d.errorEnd = num.position() + num.length();
                            diagnostic(Diag::Error_Parser_BadValueTransform_BadNumberExpr, d);
                            return false;
                        }
                        traverseDistLeft = traverseDistLeft.mid(closeSquareBracketIndex+1);
                    }else{
                        // key value based search
                        // possibleSearchExprEnclosure can be invalid
                        s.ty = PatternValueSubExpression::ExternReferenceData::NodeTraverseStep::StepType::ChildByTypeFromLookup;
                        QStringRef keyStr = traverseDistLeft.left(keyEndIndex);
                        s.kvSearchData.key = keyStr.toString();
                        if(Q_UNLIKELY(!IRNodeType::validateName(diagnostic, s.kvSearchData.key))){
                            StringDiagnosticRecord d;
                            d.str = transform;
                            // info interval is [<key>==
                            d.infoStart = traverseDistLeft.position()-1;
                            d.infoEnd = traverseDistLeft.position()+s.kvSearchData.key.length()+2;
                            // error interval is keyStr
                            d.errorStart = keyStr.position();
                            d.errorEnd = keyStr.position() + keyStr.length();
                            diagnostic(Diag::Error_Parser_BadValueTransform_InvalidNameForReference, d);
                            return false;
                        }
                        traverseDistLeft = traverseDistLeft.mid(keyEndIndex+2); // skip "=="
                        QStringRef valueStr;
                        int advanceDist = helper_getEnclosedLiteral(traverseDistLeft, valueStr, stepStart - traverseDistLeft.position());
                        if(Q_UNLIKELY(advanceDist == -1)){
                            return false;
                        }
                        s.kvSearchData.value = valueStr.toString();
                        traverseDistLeft = traverseDistLeft.mid(advanceDist);
                    }
                    stepResult.push_back(s);
                    // consume the '/' after this step, or '.' if we just finished the last level of traversal
                    if(Q_UNLIKELY(traverseDistLeft.isEmpty())){
                        StringDiagnosticRecord d;
                        d.str = transform;
                        // info interval is everything starting from bodyStart
                        d.infoStart = bodyStart.position();
                        d.infoEnd = traverseDistLeft.position() + traverseDistLeft.length();
                        // error interval is the expression start mark
                        d.errorStart = bodyStart.position() - exprStartMark.length();
                        d.errorEnd = bodyStart.position();
                        diagnostic(Diag::Error_Parser_BadValueTransform_UnterminatedExpr, d);
                        return false;
                    }
                    if(traverseDistLeft.startsWith('.')){
                        // node traverse complete
                        traverseDistLeft = traverseDistLeft.mid(1); // consume '.'
                        break;
                    }else if(Q_UNLIKELY(!traverseDistLeft.startsWith('/'))){
                        StringDiagnosticRecord d;
                        d.str = transform;
                        // info interval is stepStart to first character if traverseDistLeft
                        d.infoStart = stepStart;
                        d.infoEnd = traverseDistLeft.position()+1;
                        // error interval is the first character of traverseDistLeft
                        d.errorStart = traverseDistLeft.position();
                        d.errorEnd = traverseDistLeft.position()+1;
                        diagnostic(Diag::Error_Parser_BadValueTransform_Traverse_ExpectSlashOrDot, d);
                        return false;
                    }
                    traverseDistLeft = traverseDistLeft.mid(1); // consume '/'
                    if(Q_UNLIKELY(traverseDistLeft.isEmpty())){
                        StringDiagnosticRecord d;
                        d.str = transform;
                        // info interval is bodyStart to current position
                        d.infoStart = bodyStart.position();
                        d.infoEnd = traverseDistLeft.position();
                        // error interval is the last character we just consumed
                        d.errorStart = traverseDistLeft.position() - 1;
                        d.errorEnd = traverseDistLeft.position();
                        diagnostic(Diag::Error_Parser_BadValueTransform_ExpectTraverseExpr, d);
                        return false;
                    }
                }
                // node traversal expression complete
                // now everything left in traverseDistLeft are beginning of value reference
                int realEndMarkIndex = traverseDistLeft.indexOf(exprEndMark);
                if(Q_UNLIKELY(realEndMarkIndex == -1)){
                    StringDiagnosticRecord d;
                    d.str = transform;
                    // info interval is from bodyStart to current position
                    d.infoStart = bodyStart.position();
                    d.infoEnd = traverseDistLeft.position();
                    // error interval is the exprStartMark
                    d.errorStart = bodyStart.position() - exprStartMark.length();
                    d.errorEnd = bodyStart.position();
                    diagnostic(Diag::Error_Parser_BadValueTransform_UnterminatedExpr, d);
                    return false;
                }
                if(Q_UNLIKELY(realEndMarkIndex == 0)){
                    // no value reference
                    StringDiagnosticRecord d;
                    d.str = transform;
                    // info interval is from bodyStart to current position
                    d.infoStart = bodyStart.position();
                    d.infoEnd = traverseDistLeft.position();
                    // error interval is the last character we consumed and the exprEndMark
                    d.errorStart = traverseDistLeft.position()-1;
                    d.errorEnd = traverseDistLeft.position() + exprEndMark.length();
                    diagnostic(Diag::Error_Parser_BadValueTransform_ExpectValueName, d);
                    return false;
                }
                QStringRef referencedValueName = traverseDistLeft.left(realEndMarkIndex);
                expr.externReferenceData.valueName = referencedValueName.toString();
                if(Q_UNLIKELY(!IRNodeType::validateName(diagnostic, expr.externReferenceData.valueName))){
                    StringDiagnosticRecord d;
                    d.str = transform;
                    // info interval is everything inside expr
                    d.infoStart = bodyStart.position() - exprStartMark.length();
                    d.infoEnd = referencedValueName.position() + referencedValueName.length() + exprEndMark.length();
                    // error interval is referencedValueName
                    d.errorStart = referencedValueName.position();
                    d.errorEnd = referencedValueName.position() + referencedValueName.length();
                    diagnostic(Diag::Error_Parser_BadValueTransform_InvalidNameForReference, d);
                    return false;
                }
                // okay, everything looks good
                text = traverseDistLeft.mid(realEndMarkIndex + exprEndMark.length());
            } // done handling local / extern reference
            result.push_back(expr);
        } // done handling any non-literal
    }
    // TODO check:
    // 1. offset based traverse only allowed if going into a peer
#ifdef _MSC_VER
#pragma message ("warning: check not implemented")
#else
#warning check not implemented
#endif
    return true;
}

QList<Parser::ParserNodeData> Parser::patternMatch(QVector<QStringRef> &text, DiagnosticEmitterBase& diagnostic) const
{
    /*
     * start with root node
     * initial pattern set is made by all patterns from all possible child nodes
     * initial head is the beginning of text input
     * do the following in a loop until all input are processed:
     * 1.   if current head match with anything in ignoreList, advance the head
     * 2.   if current head match with any early exit patterns, pop path to there
     * 3.   otherwise, for each pattern in pattern set, test how many data it can consume
     * 4.   if none of the pattern applies, report error
     * 4.   add the node having longest pattern match to tree
     * 5.   if the node is an inner node, push the node to path
     *
     * pushing / poping a node:
     * 1.   replaces pattern set with all patterns from possible child node of current node
     * 2.   push / pops early exit pattern
     */

    QList<ParserNodeData> parserNodes;
    int currentParentIndex = -1;

    QVector<QStringRef> textUnits;
    textUnits.swap(text);

    // skip all leading ignored string first, then skip lines that now becomes empty
    // also reverse the order of items in the vector so that we can always read from the back
    {
        QVector<QStringRef> tempTextUnits;
        tempTextUnits.swap(textUnits);
        textUnits.reserve(tempTextUnits.size());
        while(!tempTextUnits.isEmpty()){
            QStringRef& in = tempTextUnits.back();
            context.removeLeadingIgnoredString(in);
            if(!in.isEmpty()){
                textUnits.push_back(in);
            }
            tempTextUnits.pop_back();
        }
    }

    // if we did not find any pattern match under a ParserNode, we don't bother to check any child with that type for implicit start
    // this set keeps track of which ParserNode types we don't want to waste time on
    QSet<int> implicitStartIgnoreTypeSet;

    // helper function
    // in should be back of textUnit
    auto advance = [&](QStringRef& in, int dist)->bool{
        implicitStartIgnoreTypeSet.clear();
        in = in.mid(dist);
        context.removeLeadingIgnoredString(in);
        if(in.isEmpty()){
            textUnits.pop_back();
            return true;
        }
        return false;
    };

    // bootstrap: starting the root node
    {
        const auto& root = nodes.at(0);
        ParserNodeData firstNode;
        firstNode.nodeTypeIndex = 0;
        firstNode.parentIndex = -1;
        firstNode.indexWithinParent = 0;
        firstNode.childNodeCount = 0;
        if(root.patterns.empty()){
            // if there is no pattern in root parser node, it is implicitly started
            Q_ASSERT(root.paramName.empty());
        }else{
            QHash<QString,QString> values;
            int patternIndex = -1;
            int advanceDist = 0;
            QStringRef& in = textUnits.back();
            std::tie(patternIndex, advanceDist) = findLongestMatchingPattern(root.patterns, in, values);

            if(Q_UNLIKELY(patternIndex == -1)){
                // cannot start on root
                return QList<ParserNodeData>();
            }

            advance(in, advanceDist);
            firstNode.params = performValueTransform(root.paramName, values, root.patterns.at(patternIndex).valueTransform);
        }
        parserNodes.push_back(firstNode);
        currentParentIndex = 0;
    }

    // main loop for parsing
    while(!textUnits.empty() && currentParentIndex >= 0){
        QStringRef& in = textUnits.back();
        bool isContinueMainLoop = false;
        // try patterns on all nodes along the tree path
        int parentIndex = currentParentIndex;
        while(parentIndex >= 0){
            const ParserNodeData& parentData = parserNodes.at(parentIndex);
            const Node& parent = nodes.at(parentData.nodeTypeIndex);

            int patternIndex = -1;
            int advanceDist = 0;
            QHash<QString,QString> rawValues;
            // try early exit patterns first
            std::tie(patternIndex, advanceDist) = findLongestMatchingPattern(parent.earlyExitPatterns, in, rawValues);
            if(patternIndex >= 0){
                advance(in, advanceDist);
                // we found an early exit pattern
                currentParentIndex = parentData.parentIndex;
                isContinueMainLoop = true;
                break;
            }
            // okay, nothing found
            // try all patterns from child node, and see if anything matches
            struct PatternMatchRecord{
                int nodeTypeIndex;
                int patternIndex;
                QHash<QString,QString> rawValues;
                QList<int> implicitParentNodeTypeIndexOnPath; // list of ParserNode with implicit start that lead to this match
            };

            QList<PatternMatchRecord> candidates;
            int farthestAdvanceDist = 0;
            int bestPatternScore = -1;

            auto helper_tryMatchChild = [&](int childParserNodeTypeIndex, const QList<int>& path)->void{
                const Node& childNodeTy = nodes.at(childParserNodeTypeIndex);
                int childPatternIndex = -1;
                int childAdvanceDist = 0;
                std::tie(childPatternIndex, childAdvanceDist) = findLongestMatchingPattern(childNodeTy.patterns, in, rawValues);
                if(childPatternIndex >= 0){
                    // we have a pattern that matches
                    int currentPatternScore = childNodeTy.patterns.at(childPatternIndex).priorityScore;
                    bool isAbsolutelyBetter = (childAdvanceDist > farthestAdvanceDist || (childAdvanceDist == farthestAdvanceDist && currentPatternScore > bestPatternScore));
                    if(isAbsolutelyBetter){
                        // this pattern is absolutely better than previous one; no need to keep previous results
                        farthestAdvanceDist = childAdvanceDist;
                        bestPatternScore = currentPatternScore;
                        candidates.clear();
                    }
                    if(isAbsolutelyBetter || (childAdvanceDist == farthestAdvanceDist && currentPatternScore == bestPatternScore)){
                        // at least not worse than previous ones
                        // save result of this pattern match
                        PatternMatchRecord record;
                        record.nodeTypeIndex = childParserNodeTypeIndex;
                        record.patternIndex = childPatternIndex;
                        record.rawValues.swap(rawValues);
                        record.implicitParentNodeTypeIndexOnPath = path;
                        candidates.push_back(record);
                    }
                }
                rawValues.clear();
            };

            rawValues.clear();
            int implicitStartChildCount = 0;
            for(int child : parent.allowedChildNodeIndexList){
                const Node& childNodeTy = nodes.at(child);
                if(childNodeTy.patterns.isEmpty()){
                    // these nodes are handled later on
                    Q_ASSERT(childNodeTy.paramName.empty());
                    implicitStartChildCount += 1;
                    continue;
                }

                helper_tryMatchChild(child, QList<int>());
            }

            if(candidates.isEmpty() && implicitStartChildCount > 1){
                // check childs that may start implicitly
                QQueue<QList<int>> pathQueue;
                pathQueue.reserve(implicitStartChildCount);
                for(int child : parent.allowedChildNodeIndexList){
                    // skip those we already exited from
                    if(implicitStartIgnoreTypeSet.contains(child))
                        continue;

                    const Node& childNodeTy = nodes.at(child);
                    if(childNodeTy.patterns.isEmpty() && !childNodeTy.allowedChildNodeIndexList.isEmpty()){
                        pathQueue.enqueue(QList<int>({child}));
                    }
                }
                while(!pathQueue.isEmpty()){
                    // WARNING: pathQueue.head() is not safe to "cache" since enqueue can invalidate old one
                    int tailParserNodeTypeIndex = pathQueue.head().back();
                    const Node& ty = nodes.at(tailParserNodeTypeIndex);
                    // go through all its child
                    // if a child has patterns (not implicit start), try these patterns
                    // otherwise, enqueue another path with that child at back
                    for(int child : ty.allowedChildNodeIndexList){
                        if(implicitStartIgnoreTypeSet.contains(child))
                            continue;

                        const Node& childNodeTy = nodes.at(child);
                        if(childNodeTy.patterns.isEmpty()){
                            Q_ASSERT(childNodeTy.paramName.empty());
                            QList<int> newPath(pathQueue.head());
                            newPath.push_back(child);
                            pathQueue.enqueue(newPath);
                        }else{
                            helper_tryMatchChild(child, pathQueue.head());
                        }
                    }
                    pathQueue.dequeue();
                }
            }

            if(candidates.isEmpty()){
                // no pattern match
                // go to parent
                if(parent.patterns.isEmpty()){
                    implicitStartIgnoreTypeSet.insert(parentData.nodeTypeIndex);
                }
                parentIndex = parentData.parentIndex;
                continue;
            }else{
                if(Q_UNLIKELY(candidates.size() > 1)){
                    // ambiguous scenario; we will just select the first pattern
                    // report a warning though
                    QList<QVariant> errorArgs;
                    QStringRef ambiguousText = in.left(farthestAdvanceDist);
                    context.removeTrailingIgnoredString(ambiguousText);
                    errorArgs.push_back(ambiguousText.toString());
                    for(const auto& data: candidates){
                        QVariantList vlist; // [NodeName][NodeParamStringList][PatternIndex]
                        const Node& childNodeTy = nodes.at(data.nodeTypeIndex);
                        vlist.push_back(childNodeTy.nodeName);
                        QStringList params = performValueTransform(
                                    childNodeTy.paramName,
                                    data.rawValues,
                                    childNodeTy.patterns.at(data.patternIndex).valueTransform);
                        vlist.push_back(params);
                        vlist.push_back(data.patternIndex);
                        errorArgs.push_back(vlist);
                    }
                    diagnostic.handle(Diag::Warn_Parser_Matching_Ambiguous, errorArgs);
                }

                // at least one match; pick the first
                const PatternMatchRecord& record = candidates.front();

                // if there are implicitly started nodes, create them first
                for(int nodeOnPath : record.implicitParentNodeTypeIndexOnPath){
                    ParserNodeData intermediateNodeData;
                    intermediateNodeData.parentIndex = parentIndex;
                    intermediateNodeData.nodeTypeIndex = nodeOnPath;
                    intermediateNodeData.indexWithinParent = parserNodes[parentIndex].childNodeCount++;
                    intermediateNodeData.childNodeCount = 0;
                    intermediateNodeData.params.clear();// implicitly started node won't have parameters
                    parentIndex = parserNodes.size();
                    parserNodes.push_back(intermediateNodeData);
                }

                // create the end node
                const Node& childNodeTy = nodes.at(record.nodeTypeIndex);
                ParserNodeData endData;
                endData.parentIndex = parentIndex;
                endData.nodeTypeIndex = record.nodeTypeIndex;
                endData.indexWithinParent = parserNodes[parentIndex].childNodeCount++;
                endData.childNodeCount = 0;
                endData.params = performValueTransform(
                            childNodeTy.paramName,
                            record.rawValues,
                            childNodeTy.patterns.at(record.patternIndex).valueTransform);
                if(childNodeTy.allowedChildNodeIndexList.isEmpty()){
                    // leaf node; no change on parent
                    currentParentIndex = parentIndex;
                }else{
                    currentParentIndex = parserNodes.size();
                }
                parserNodes.push_back(endData);
                advance(in, farthestAdvanceDist);
                isContinueMainLoop = true;
                break;
            }
        }
        if(Q_LIKELY(isContinueMainLoop))
            continue;

        // if we reach here, it means that we reached the root and no patterns can be applied so far
        Q_ASSERT(parentIndex == -1);
        Q_ASSERT(!textUnits.isEmpty());
        diagnostic(Diag::Error_Parser_Matching_NoMatch);
        return QList<ParserNodeData>();
    }

    // if we reach here, it either means that all texts are consumed, or an early exit pattern of root is found
    while(!textUnits.empty()){
        QStringRef& in = textUnits.back();
        int len = context.removeLeadingIgnoredString(in);
        if(len > 0){
            advance(in, len);
        }else{
            if(!in.isEmpty()){
                // garbage at the end
                diagnostic(Diag::Error_Parser_Matching_GarbageAtEnd);
                return QList<ParserNodeData>();
            }
        }
    }

    // now all the text are successfully consumed
    return parserNodes;
}

std::pair<bool,QString> Parser::IRBuildContext::solveExternReference(const Parser& p, const PatternValueSubExpression::ExternReferenceData &expr, int nodeIndex)
{
    std::pair<bool,QString> fail(false, QString());
    int currentNodeIndex = nodeIndex;
    if(expr.isTraverseStartFromRoot){
        currentNodeIndex = 0;
    }
    // at time of writing, 4 traverse type is supported: Parent, ChildByTypeAndOrder, ChildByTypeFromLookup, and AnyChildByOrder
    for(const auto& step : expr.nodeTraversal){
        // handle the easiest case that go to parent first
        if(step.ty == PatternValueSubExpression::ExternReferenceData::NodeTraverseStep::StepType::Parent){
            // do not go before root (doing .. on root evaluates to root itself)
            if(currentNodeIndex > 0){
                currentNodeIndex = parserNodes.at(currentNodeIndex).parentIndex;
            }
            continue;
        }
        // now it is going into child
        const ParserNodeData& curNode = parserNodes.at(currentNodeIndex);
        const Node& curNodeTy = p.nodes.at(curNode.nodeTypeIndex);
        if(step.ty == PatternValueSubExpression::ExternReferenceData::NodeTraverseStep::StepType::AnyChildByOrder){
            // get the list of candidate nodes
            const QList<int>& children = getNodeChildList(currentNodeIndex, -1);
            int childIndex = step.ioSearchData.lookupNum;
            if(!step.ioSearchData.isNumIndexInsteadofOffset){
                // offset based search
                childIndex += parserNodes.at(nodeIndex).indexWithinParent;
            }
            if(childIndex < 0 || childIndex >= children.size()){
                // out of bound; bad index
                return fail;
            }
            currentNodeIndex = children.at(childIndex);
            continue;
        }
        // now we must be indexing into child with specific type
        // we should have a valid child name in this case
        // get the childTypeIndex by searching through names
        int childTypeIndex = -1;
        for(int child : curNodeTy.allowedChildNodeIndexList){
            const Node& curChild = p.nodes.at(child);
            if(curChild.nodeName == step.childParserNodeName){
                childTypeIndex = child;
                break;
            }
        }
        if(childTypeIndex == -1){
            // no such child found; error
            return fail;
        }

        // get the list of candidate nodes
        const QList<int>& children = getNodeChildList(currentNodeIndex, childTypeIndex);

        if(step.ty == PatternValueSubExpression::ExternReferenceData::NodeTraverseStep::StepType::ChildByTypeFromLookup){
            const Node& childTy = p.nodes.at(childTypeIndex);
            int paramIndex = childTy.paramName.indexOf(step.kvSearchData.key);
            // the validation code should reject code like this
            Q_ASSERT(paramIndex != -1);

            // if the parent node is the same:
            // use the first match "before" current node, if there is one
            // otherwise, use the first match "after" current node, if there is one
            // otherwise, fail
            // if the parent node is not the same: just go through the list from bottom to top and stop at first result
            int startIndex = children.size() - 1;
            if(curNode.parentIndex == parserNodes.at(nodeIndex).parentIndex){
                // find the first child node in given type that is either before or is current node
                auto iter = std::upper_bound(children.begin(), children.end(), nodeIndex);
                startIndex = static_cast<int>(std::distance(children.begin(), iter))-1;
            }

            bool isFound = false;
            for(int i = startIndex; i >= 0; --i){
                int child = children.at(i);
                const ParserNodeData& curChild = parserNodes.at(child);
                if(curChild.params.at(paramIndex) == step.kvSearchData.value){
                    currentNodeIndex = child;
                    isFound = true;
                    break;
                }
            }
            if(isFound)
                continue;

            // search from the startIndex downward
            for(int i = startIndex+1; i < children.size(); ++i){
                int child = children.at(i);
                const ParserNodeData& curChild = parserNodes.at(child);
                if(curChild.params.at(paramIndex) == step.kvSearchData.value){
                    currentNodeIndex = child;
                    isFound = true;
                    break;
                }
            }
            if(!isFound)
                return fail;
        }else{
            // otherwise we are just searching by order
            Q_ASSERT(step.ty == PatternValueSubExpression::ExternReferenceData::NodeTraverseStep::StepType::AnyChildByOrder);
            int childIndex = step.ioSearchData.lookupNum;
            if(!step.ioSearchData.isNumIndexInsteadofOffset){
                // offset based search
                auto iter = std::upper_bound(children.begin(), children.end(), nodeIndex);
                int baseIndex = static_cast<int>(std::distance(children.begin(), iter))-1;
                childIndex += baseIndex;
            }
            if(childIndex < 0 || childIndex >= children.size()){
                // out of bound; bad index
                return fail;
            }
            currentNodeIndex = children.at(childIndex);
            continue;
        }
    }
    // okay, all steps are done
    // access the member
    const ParserNodeData& data = parserNodes.at(currentNodeIndex);
    int paramIndex = p.nodes.at(data.nodeTypeIndex).paramName.indexOf(expr.valueName);
    if(paramIndex == -1){
        // no such member under the node
        return fail;
    }
    Q_ASSERT(paramIndex < data.params.size());
    return std::make_pair(true, data.params.at(paramIndex));
#if 0
    int splitterIndex = expr.indexOf(QChar(':'));
    if(splitterIndex == -1){
        return fail;
    }
    if((splitterIndex == expr.length()-1) || expr.at(splitterIndex+1) != ':'){
        // just a single ':', malformed expression
        return fail;
    }

    QStringRef paramName = expr.midRef(splitterIndex+2);
    QStringRef nodeExpr = expr.leftRef(splitterIndex);

    QVector<QStringRef> steps = nodeExpr.split('/');
    int currentNodeIndex = nodeIndex;
    for(QStringRef step : steps){
        // skip empty parts
        if(step.isEmpty() || step == ".")
            continue;

        const ParserNodeData& curNode = parserNodes.at(currentNodeIndex);
        if(step == ".."){
            // do not go before root (doing .. on root evaluates to root itself)
            if(currentNodeIndex > 0){
                currentNodeIndex = curNode.parentIndex;
            }
        }else{
            int childTypeIndex = -1;
            int childIndex = -1;

            QStringRef childName;
            QStringRef enclosedExpr;
            int exprStart = step.indexOf(QChar('['));
            if(exprStart == -1){
                childName = step;
            }else{
                // either [<IndexExpr>] or <ChildName>[<LookupExpr>] expected
                if(step.back() != ']'){
                    // malformed expression
                    return fail;
                }
                childName = step.left(exprStart);
                enclosedExpr = step.mid(exprStart+1).chopped(1);// skip []
            }

            const Node& curNodeTy = p.nodes.at(curNode.nodeTypeIndex);

            if(!childName.isEmpty()){
                // get the childTypeIndex by searching through names
                for(int child : curNodeTy.allowedChildNodeIndexList){
                    const Node& curChild = p.nodes.at(child);
                    if(curChild.nodeName == childName){
                        childTypeIndex = child;
                        break;
                    }
                }
                if(childTypeIndex == -1){
                    // no such child found; error
                    return fail;
                }
            }

            // perform index based search
            bool isIndexBasedSearch = false;
            bool isPeerNodeAccess = false;
            if(enclosedExpr.isEmpty()){
                isIndexBasedSearch = true;
                childIndex = 0;
            }else{
                // it is not a key based search if everything in enclosedExpr is a valid integer
                bool isGood = false;
                childIndex = enclosedExpr.toInt(&isGood);
                if(isGood){
                    isIndexBasedSearch = true;
                    if(enclosedExpr.front() == '+' || enclosedExpr.front() == '-'){
                        // offset based index expression; childIndex is not the final value
                        if(childTypeIndex != -1){
                            // there is a child node type specified
                            // only allowed when we access peers
                            if(currentNodeIndex == parserNodes.at(nodeIndex).parentIndex){
                                isPeerNodeAccess = true;
                            }else{
                                return fail;
                            }
                        }else{
                            // no child node type specified
                            // get the "cooked" index now
                            childIndex += parserNodes.at(nodeIndex).indexWithinParent;
                        }
                    }
                }
            }

            // get the list of candidate nodes
            const QList<int>& children = getNodeChildList(currentNodeIndex, childTypeIndex);

            if(isPeerNodeAccess){
                // find the first child node in given type that is either before or is current node
                auto iter = std::upper_bound(children.begin(), children.end(), nodeIndex);
                int baseIndex = static_cast<int>(std::distance(children.begin(), iter)) - 1;
                // fix-up the final index
                childIndex += baseIndex;
            }

            if(isIndexBasedSearch){
                if(childIndex < 0 || childIndex >= children.size()){
                    // out of bound; bad index
                    return fail;
                }
                currentNodeIndex = children.at(childIndex);
                continue;
            }

            // key based search
            // extract the key and value field first
            int equalSignIndex = enclosedExpr.indexOf(QChar('='));
            if(equalSignIndex == -1){
                // no equal sign found; malformed LookupExor
                return fail;
            }
            if(equalSignIndex+2 >= enclosedExpr.size() || enclosedExpr.at(equalSignIndex+1) != '='){
                // it is not a "==" or there is nothing after "=="; malformed LookupExor
                return fail;
            }
            QStringRef lookupKey = enclosedExpr.left(equalSignIndex);
            QStringRef lookupValue = enclosedExpr.mid(equalSignIndex+2);
            // for now we require lookupValue to be a '"' enclosed string literal
            if(!(lookupValue.startsWith('"') && lookupValue.endsWith('"'))){
                // malformed
                return fail;
            }
            // remove the quote
            lookupValue = lookupValue.mid(1);
            lookupValue.chop(1);
            // get the parameter index from the lookupKey
            const Node& childNodeTy = p.nodes.at(childIndex);
            int paramIndex = -1;
            for(int i = 0, n = childNodeTy.paramName.size(); i < n; ++i){
                if(childNodeTy.paramName.at(i) == lookupKey){
                    paramIndex = i;
                    break;
                }
            }
            if(paramIndex == -1){
                // no parameter with specified name found; fail
                return fail;
            }
            int currentCandidate = -1;
            for(int child : children){
                const ParserNodeData& childData = parserNodes.at(child);
                if(childData.params.at(paramIndex) == lookupValue){
                    if(currentCandidate == -1){
                        currentCandidate = child;
                    }else{
                        // duplicated search result; fail
                        return fail;
                    }
                }
            }
            currentNodeIndex = currentCandidate;
            continue;
        }
    }

    // node traversal complete; read the value and done
    const ParserNodeData& nodeData = parserNodes.at(currentNodeIndex);
    const Node& nodeTy = p.nodes.at(nodeData.nodeTypeIndex);
    for(int i = 0, n = nodeTy.paramName.size(); i < n; ++i){
        if(nodeTy.paramName.at(i) == paramName){
            return std::make_pair(true, nodeData.params.at(i));
        }
    }

    // no parameter with given name found
    return fail;
#endif
}

const QList<int>& Parser::IRBuildContext::getNodeChildList(int parentIndex, int childNodeTypeIndex)
{
    auto& nodeCache = parserNodeChildListCache[parentIndex];
    {
        auto iter = nodeCache.find(childNodeTypeIndex);
        if(iter != nodeCache.end())
            return iter.value();
    }
    // okay, it is not in cache
    // let's compute it

    if(nodeCache.empty()){
        // we have not yet built any list
        // let's prepare the list of all child nodes at the same time
        QList<int> allChildList;
        QList<int> wantedChildList;
        // we go through all ParserNodes after the node specified by parentIndex:
        // 1. If a node's parent index is smaller than parentIndex, it means that we already went outside the subtree under parent
        //    and allChildList is already complete
        // 2. If a node's parent index is equal to parentIndex, add it to allChildList since it is a direct child
        // 3. If a node's parent index is larger than parentIndex, it is an indirect child
        for(int curNodeIndex = parentIndex+1, numNode = parserNodes.size(); curNodeIndex < numNode; ++curNodeIndex){
            const auto& d = parserNodes.at(curNodeIndex);
            if(d.parentIndex == parentIndex){
                allChildList.push_back(curNodeIndex);

                // if we also looks for child with specific type, we add them here
                if(childNodeTypeIndex != -1 && d.nodeTypeIndex == childNodeTypeIndex){
                    wantedChildList.push_back(curNodeIndex);
                }
            }else if(d.parentIndex < parentIndex){
                break;
            }
        }
        nodeCache.insert(-1, allChildList);
        if(childNodeTypeIndex != -1){
            nodeCache.insert(childNodeTypeIndex, wantedChildList);
        }
        // we may have childNodeTypeIndex == -1 here
        return nodeCache[childNodeTypeIndex];
    }else{
        // we already have the list of all children
        // build the wanted list from the list of all direct childs
        Q_ASSERT(childNodeTypeIndex != -1);
        auto iter_allChild = nodeCache.find(-1);
        Q_ASSERT(iter_allChild != nodeCache.end());
        QList<int> wantedChildList;
        for(int child: iter_allChild.value()){
            const auto& d = parserNodes.at(child);
            if(d.nodeTypeIndex == childNodeTypeIndex){
                wantedChildList.push_back(child);
            }
        }
        return nodeCache.insert(childNodeTypeIndex, wantedChildList).value();
    }
}

IRRootInstance* Parser::parse(QVector<QStringRef> &text, const IRRootType& ir, DiagnosticEmitterBase& diagnostic) const
{
    IRBuildContext ctx;
    ctx.parserNodes = patternMatch(text, diagnostic);
    if(ctx.parserNodes.empty())
        return nullptr;

    // start to build IR tree
    std::unique_ptr<IRRootInstance> ptr(new IRRootInstance(ir));
    // root node to root node
    const ParserNodeData& rootData = ctx.parserNodes.front();
    const Node& rootNodeTy = nodes.at(rootData.nodeTypeIndex);

    auto helper_buildIRNode = [&](int parserNodeIndex, int irNodeTypeIndex, int parentIRNodeIndex)->int{
        const IRNodeType& irNodeTy = ptr->getType().getNodeType(irNodeTypeIndex);

        // step 1: insert node
        int irNodeIndex = ptr->addNode(irNodeTypeIndex);
        auto& node = ptr->getNode(irNodeIndex);
        node.setParent(parentIRNodeIndex);
        if(parentIRNodeIndex != -1){
            ptr->getNode(parentIRNodeIndex).addChildNode(irNodeIndex);
        }

        // step 2: prepare value transform
        QList<QVariant> params;
        const ParserNodeData& nodeData = ctx.parserNodes.at(parserNodeIndex);
        const Node& nodeTy = nodes.at(nodeData.nodeTypeIndex);
        Q_ASSERT(nodeTy.combineValueTransform.empty() || nodeTy.combineValueTransform.size() == irNodeTy.getNumParameter());
        QHash<QString,QString> nodeStrData;
        for(int i = 0, n = nodeTy.paramName.size(); i < n; ++i){
            nodeStrData.insert(nodeTy.paramName.at(i), nodeData.params.at(i));
        }
        for(int i = 0, n = irNodeTy.getNumParameter(); i < n; ++i){
            QString value;
            QString irParamName = irNodeTy.getParameterName(i);
            if(nodeTy.combineValueTransform.empty() || nodeTy.combineValueTransform.at(i).empty()){
                // search for ParserNode parameter with the same name
                // use it as the final value
                int idx = nodeTy.paramName.indexOf(irParamName);
                value = nodeData.params.at(idx);
            }else{
                const auto& exprList = nodeTy.combineValueTransform.at(i);
                bool isAnyExprGood = false;
                for(const auto& expr : exprList){
                    bool isExprGood = true;
                    value = performValueTransform(irParamName, nodeStrData, expr, [&](const PatternValueSubExpression::ExternReferenceData& e)->QString{
                        QString retVal;
                        bool isThisOneGood = true;
                        std::tie(isThisOneGood, retVal) = ctx.solveExternReference(*this, e, parserNodeIndex);
                        isExprGood = isExprGood && isThisOneGood;
                        return retVal;
                    });
                    if(isExprGood){
                        isAnyExprGood = true;
                        break;
                    }
                }
                if(Q_UNLIKELY(!isAnyExprGood)){
                    diagnostic(Diag::Error_Parser_IRBuild_BadTransform, nodeTy.nodeName, irNodeTy.getName(), irParamName);
                    return -1;
                }
            }
            // cast value to IR type
            QVariant irValue;
            ValueType irValTy = irNodeTy.getParameterType(i);
            switch(irValTy){
            default: Q_UNREACHABLE(); break;
            case ValueType::String:{
                irValue = value;
            }break;
            case ValueType::Int64:{
                bool isGood = true;
                irValue.setValue(value.toLongLong(&isGood));
                if(!isGood){
                    diagnostic(Diag::Error_Parser_IRBuild_BadCast, nodeTy.nodeName, irNodeTy.getName(), irParamName, irValTy, value);
                    return -1;
                }
            }break;
            }

            params.push_back(irValue);
        }
        node.setParameters(params);

        return irNodeIndex;
    };
    int rootNodeIRIndex = helper_buildIRNode(0, rootNodeTy.combineToIRNodeIndex, -1);
    Q_ASSERT(rootNodeIRIndex == 0);

    struct NodeIndexRecord{
        int parserNodeIndex;
        int irNodeIndex;
    };
    QList<NodeIndexRecord> parentStack;
    parentStack.push_back(NodeIndexRecord{0,0});

    for(int parserNodeIndex = 1, n = ctx.parserNodes.size(); parserNodeIndex < n; ++parserNodeIndex){
        const ParserNodeData& nodeData = ctx.parserNodes.at(parserNodeIndex);
        const Node& nodeTy = nodes.at(nodeData.nodeTypeIndex);

        // ignore parser-only nodes
        int irNodeTypeIndex = nodeTy.combineToIRNodeIndex;
        if(irNodeTypeIndex == -1)
            continue;

        Q_ASSERT(irNodeTypeIndex >= 0);

        // find the correct parent IR node
        // keep popping parentStack until the stack top appears in the parent of current parser node
        int curParentParserNodeIndex = nodeData.parentIndex;
        while(!parentStack.isEmpty() && (parentStack.back().parserNodeIndex != curParentParserNodeIndex)){
            if(parentStack.back().parserNodeIndex > curParentParserNodeIndex){
                parentStack.pop_back();
            }else{
                curParentParserNodeIndex = ctx.parserNodes.at(curParentParserNodeIndex).parentIndex;
            }
        }
        Q_ASSERT(!parentStack.isEmpty());
        Q_ASSERT(parentStack.back().parserNodeIndex == curParentParserNodeIndex);
        int irNodeIndex = helper_buildIRNode(parserNodeIndex, irNodeTypeIndex, parentStack.back().irNodeIndex);
        if(irNodeIndex == -1){
            // error occurred
            return nullptr;
        }
        parentStack.push_back(NodeIndexRecord{parserNodeIndex, irNodeIndex});
    }

    if(ptr->validate(diagnostic)){
        return ptr.release();
    }

    // IR fails to validate in this case
    return nullptr;
}

std::pair<int,int> Parser::findLongestMatchingPattern(const QList<Pattern>& patterns, QStringRef text, QHash<QString,QString>& values) const
{
    int bestPatternIndex = -1;
    int bestPatternScore = -1;
    int numConsumed = 0;
    QHash<QString, QString> curRawValue;

    for(int i = 0, num = patterns.size(); i < num; ++i){
        int curConsumeCount = match(text, curRawValue, context, patterns.at(i).elements);
        if(curConsumeCount > numConsumed || (curConsumeCount == numConsumed && patterns.at(i).priorityScore > bestPatternScore)){
            bestPatternIndex = i;
            bestPatternScore = patterns.at(i).priorityScore;
            numConsumed = curConsumeCount;
            values.swap(curRawValue);
        }
        curRawValue.clear();
    }

    return std::make_pair(bestPatternIndex, numConsumed);
}

std::pair<int,int> Parser::ParseContext::getMatchingEndAdvanceDistance(QStringRef text, int matchPairIndex)const
{
    // for each candidate end string, find their first occurrence
    int pos = -1;
    int reach = -1;
    const QStringList& list = matchPairEnds.at(matchPairIndex);
    for(const QString& candidateEnd : list){
        int curIndex = text.indexOf(candidateEnd);
        if(curIndex != -1){
            if(pos == -1 || curIndex < pos || ((curIndex == pos) && (curIndex + candidateEnd.length() > reach))){
                pos = curIndex;
                reach = curIndex + candidateEnd.length();
            }
        }
    }
    // no any match pair end found
    if(pos == -1)
        return std::make_pair(-1,-1);

    // try to find if there is any match pair start mark between 0 and pos
    // if there is, recursively find its end mark, and start searching from there again
    int searchTextLen = reach - 1 + longestMatchPairStartStringLength;
    int nextMatchPairPos = -1;
    int nextMatchPairIndex = -1;
    int nextMatchPairStartLength = -1;
    std::tie(nextMatchPairPos, nextMatchPairIndex, nextMatchPairStartLength) = getMatchingStartAdvanceDistance(text.left(searchTextLen));
    if(nextMatchPairPos == -1 || nextMatchPairPos > pos || (nextMatchPairPos == pos && nextMatchPairStartLength < (reach - pos))){
        // no match pair start found, or the match pair start is not "early" in text enough (partial overlap with end marker)
        // done
        return std::make_pair(reach, reach - pos);
    }

    // we found a match pair start
    // find its end then
    int nextSearchStart = nextMatchPairPos + nextMatchPairStartLength;
    int endAdvance = getMatchingEndAdvanceDistance(text.mid(nextSearchStart), nextMatchPairIndex).first;
    if(endAdvance == -1){
        // no match for nested match pair; fail
        return std::make_pair(-1,-1);
    }
    int recurseStart = nextSearchStart + endAdvance;
    int endLen = -1;
    int recurseAdvance = -1;
    std::tie(recurseAdvance, endLen) = getMatchingEndAdvanceDistance(text.mid(recurseStart), matchPairIndex);
    if(recurseAdvance == -1){
        // no match for this match pair
        return std::make_pair(-1,-1);
    }
    return std::make_pair(recurseStart + recurseAdvance, endLen);
}

std::tuple<int, int, int> Parser::ParseContext::getMatchingStartAdvanceDistance(QStringRef text)const
{
    int pos = -1;
    int matchPairIndex = -1;
    int matchPairStartLength = -1;

    for(int i = 0, n = matchPairStarts.size(); i < n; ++i){
        for(const QString& str : matchPairStarts.at(i)){
            int index = text.indexOf(str);
            if(index != -1){
                if(pos == -1 || index < pos || (index == pos && matchPairStartLength < str.length())){
                    pos = index;
                    matchPairIndex = i;
                    matchPairStartLength = str.length();
                }
            }
        }
    }

    return std::make_tuple(pos, matchPairIndex, matchPairStartLength);
}

int Parser::ParseContext::removeTrailingIgnoredString(QStringRef& ref)const
{
    bool isChangeMade = true;
    int trimLen = 0;
    while(isChangeMade && !ref.isEmpty()){
        isChangeMade = false;
        for(const QString& ignore : ignoreList){
            while(ref.endsWith(ignore)){
                ref.chop(ignore.length());
                trimLen += ignore.length();
                isChangeMade = true;
            }
        }
    }
    return trimLen;
}

int Parser::ParseContext::removeLeadingIgnoredString(QStringRef& ref)const
{
    bool isChangeMade = true;
    int trimLen = 0;
    while(isChangeMade && !ref.isEmpty()){
        isChangeMade = false;
        for(const QString& ignore : ignoreList){
            while(ref.startsWith(ignore)){
                ref = ref.mid(ignore.length());
                trimLen += ignore.length();
                isChangeMade = true;
            }
        }
    }
    return trimLen;
}

int Parser::match(QStringRef input, QHash<QString,QString>& values, const ParseContext &ctx, const QList<SubPattern>& pattern)
{
    values.clear();
    if(pattern.empty())
        return 0;

    // helper function
    auto regexExtractMatchValues = [&](const QStringList& namedCaptureGroups, const QRegularExpressionMatch& matchResult)->void{
        // the first capture should have no name
        Q_ASSERT(namedCaptureGroups.front().isEmpty());
        for(int i = 1; i < namedCaptureGroups.size(); ++i){
            const QString& name = namedCaptureGroups.at(i);
            if(!name.isEmpty()){
                values.insert(name, matchResult.captured(i));
            }
        }
    };

    // helper function
    // return number of characters consumed if successful; used inside Auto type subpattern
    auto autoSubPattern_checkNextPattern = [&regexExtractMatchValues](const SubPattern& p, QStringRef text, const ParseContext& ctx)->int{
        switch(p.ty){
        case SubPatternType::Auto:{
            Q_UNREACHABLE();
        }break;
        case SubPatternType::Literal:{
            if(text.startsWith(p.literalData.str))
                return p.literalData.str.length();
            return 0;
        }
        case SubPatternType::Regex:{
            auto regexResult = p.regexData.regex.match(text, 0, QRegularExpression::NormalMatch, QRegularExpression::AnchoredMatchOption);
            if(!regexResult.hasMatch()){
                // no matches
                return 0;
            }
            regexExtractMatchValues(p.regexData.regex.namedCaptureGroups(), regexResult);
            return regexResult.capturedEnd(0);
        }
        case SubPatternType::MatchPair:{
            const auto& data = p.matchPairData;
            int forwardDist = 0;
            const QStringList& matchPairMarkerList = (data.isStart? ctx.matchPairStarts : ctx.matchPairEnds).at(data.matchPairIndex);
            for(const QString& mark : matchPairMarkerList){
                if(text.startsWith(mark)){
                    if(mark.length() > forwardDist){
                        forwardDist = mark.length();
                    }
                }
            }
            return forwardDist;
        }
        }
        Q_UNREACHABLE();
    };

    QStringRef text = input;
    for(int patternIndex = 0, n = pattern.size(); patternIndex < n; ++patternIndex){
        ctx.removeLeadingIgnoredString(text);
        if(text.isEmpty())
            return 0;

        const SubPattern& p = pattern.at(patternIndex);
        switch(p.ty){
        case SubPatternType::Literal:{
            if(!text.startsWith(p.literalData.str)){
                // match fail
                return 0;
            }
            text = text.mid(p.literalData.str.length());
        }break;
        case SubPatternType::Regex:{
            auto result = p.regexData.regex.match(text, 0, QRegularExpression::NormalMatch, QRegularExpression::AnchoredMatchOption);
            if(!result.hasMatch()){
                // match fail
                return 0;
            }
            regexExtractMatchValues(p.regexData.regex.namedCaptureGroups(), result);
            text = text.mid(result.capturedRef(0).length());
        }break;
        case SubPatternType::MatchPair:{
            const auto& data = p.matchPairData;
            int forwardDist = 0;
            const QStringList& matchPairMarkerList = (data.isStart? ctx.matchPairStarts : ctx.matchPairEnds).at(data.matchPairIndex);
            for(const QString& mark : matchPairMarkerList){
                if(text.startsWith(mark)){
                    if(mark.length() > forwardDist){
                        forwardDist = mark.length();
                    }
                }
            }
            if(forwardDist == 0){
                // we did not find the match pair start/end string
                // match fail
                return 0;
            }
            text = text.mid(forwardDist);
        }break;
        case SubPatternType::Auto:{
            const SubPattern::AutoData& data = p.autoData;
            QStringRef valueBody = text; // make a copy at starting position
            QStringRef nextMatchData;

            bool isMatchMade = false;
            while(!text.isEmpty()){
                // check if the stop condition is reached
                if(data.isTerminateByIgnoredString){
                    for(const QString& ignored : ctx.ignoreList){
                        if(text.startsWith(ignored)){
                            valueBody.chop(text.length());
                            text = text.mid(ignored.length());
                            isMatchMade = true;
                            break;
                        }
                    }
                    if(isMatchMade)
                        break;
                }else{
                    int dist = autoSubPattern_checkNextPattern(pattern.at(patternIndex+1), text, ctx);
                    if(dist != 0){
                        valueBody.chop(text.length());
                        nextMatchData = text.left(dist);
                        text = text.mid(dist);
                        ctx.removeTrailingIgnoredString(valueBody);
                        isMatchMade = true;
                        break;
                    }
                }

                // no match for next pattern
                // see if we have a starting match pair, if yes then fast forward until the end of match pair
                int matchPairStartMaxLen = 0;
                int matchPairIndex = -1;
                for(int i = 0, n = ctx.matchPairStarts.size(); i < n; ++i){
                    for(const QString& start : ctx.matchPairStarts.at(i)){
                        if(text.startsWith(start)){
                            if(matchPairIndex == -1 || matchPairStartMaxLen < start.length()){
                                matchPairIndex = i;
                                matchPairStartMaxLen = start.length();
                            }
                        }
                    }
                }
                if(matchPairIndex == -1){
                    // not starting a match pair enclosed block
                    // just forward one character
                    text = text.mid(1);
                }else{
                    // starting a match pair
                    text = text.mid(matchPairStartMaxLen);
                    auto resultPair = ctx.getMatchingEndAdvanceDistance(text, matchPairIndex);
                    if(resultPair.first == -1){
                        // no end found; bad match
                        return 0;
                    }
                    text = text.mid(resultPair.first);
                    ctx.removeLeadingIgnoredString(text);
                }
            }
            if(!isMatchMade){
                // we allow termination by ignored string to end without finding an ignored string
                // but not for others
                if(!data.isTerminateByIgnoredString){
                    return 0;
                }
            }
            // also increment the patternIndex here since we consumed the next pattern already
            if(!data.isTerminateByIgnoredString){
                patternIndex += 1;
            }
            // extract value if needed
            if(!data.valueName.isEmpty()){
                QString value = valueBody.toString();
                if(data.nextSubPatternIncludeLength != 0){
                    if(data.nextSubPatternIncludeLength == -1){
                        value.append(nextMatchData);
                    }else{
                        value.append(nextMatchData.left(data.nextSubPatternIncludeLength));
                    }
                }
                values.insert(data.valueName, value);
            }
        }break;
        }
    }
    // if we reach here then all patterns are successfully matched
    return text.position() - input.position();
}

QStringList Parser::performValueTransform(const QStringList& paramName, const QHash<QString,QString>& rawValues, const QList<QList<PatternValueSubExpression> > &valueTransform)
{
    QStringList result;
    Q_ASSERT(paramName.size() == valueTransform.size());

    for(int i = 0, num = paramName.size(); i < num; ++i){
        const QString& param = paramName.at(i);
        const auto& list = valueTransform.at(i);
        QString value;
        if(list.isEmpty()){
            // direct search from raw values
            value = rawValues.value(param);
        }else{
            for(const auto& expr : list){
                switch(expr.ty){
                case PatternValueSubExpression::OpType::Literal:{
                    value.append(expr.literalData.str);
                }break;
                case PatternValueSubExpression::OpType::LocalReference:{
                    Q_ASSERT(rawValues.count(expr.localReferenceData.valueName) > 0);
                    value.append(rawValues.value(expr.localReferenceData.valueName));
                }break;
                case PatternValueSubExpression::OpType::ExternReference:{
                    Q_UNREACHABLE();
                }
                }
            }
        }
        result.push_back(value);
    }

    return result;
}

QString Parser::performValueTransform(const QString &paramName,
        const QHash<QString,QString>& rawValues,
        const QList<PatternValueSubExpression> &valueTransform,
        std::function<QString(const PatternValueSubExpression::ExternReferenceData&)> externReferenceSolver)
{
    if(valueTransform.isEmpty())
        return rawValues.value(paramName);

    QString value;
    for(const auto& expr : valueTransform){
        switch(expr.ty){
        case PatternValueSubExpression::OpType::Literal:{
            value.append(expr.literalData.str);
        }break;
        case PatternValueSubExpression::OpType::LocalReference:{
            Q_ASSERT(rawValues.count(expr.localReferenceData.valueName) > 0);
            value.append(rawValues.value(expr.localReferenceData.valueName));
        }break;
        case PatternValueSubExpression::OpType::ExternReference:{
            value.append(externReferenceSolver(expr.externReferenceData));
        }break;
        }
    }

    return value;
}
