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

    // check match pairs:
    // 1. match pairs should have unique names (and the name must be good)
    // 2. no match pair string (either start or end) can be identical to any other match pair string (either start or end)

    return nullptr;
}

QList<Parser::ParserNodeData> Parser::patternMatch(const QString& text, DiagnosticEmitterBase& diagnostic) const
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

    if(context.isLineMode){
        textUnits = text.splitRef(QChar('\n'), QString::SkipEmptyParts, Qt::CaseSensitive);
    }else{
        textUnits.push_back(QStringRef(&text, 0, text.length()));
    }

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

std::pair<bool,QString> Parser::IRBuildContext::solveExternReference(const Parser& p, const QString& expr, int nodeIndex)
{
    std::pair<bool,QString> fail(false, QString());
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

IRRootInstance* Parser::parse(const QString& text, const IRRootType& ir, DiagnosticEmitterBase& diagnostic) const
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
    if(rootNodeTy.combineToIRNodeTypeName.isEmpty()){
        diagnostic(Diag::Error_Parser_IRBuild_BadRoot);
        return nullptr;
    }

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
                    value = performValueTransform(irParamName, nodeStrData, expr, [&](const QString& e)->QString{
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
    int rootNodeIRTyIndex = ir.getNodeTypeIndex(rootNodeTy.combineToIRNodeTypeName);
    int rootNodeIRIndex = helper_buildIRNode(0, rootNodeIRTyIndex, -1);
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
        if(nodeTy.combineToIRNodeTypeName.isEmpty())
            continue;

        int irNodeTypeIndex = ir.getNodeTypeIndex(nodeTy.combineToIRNodeTypeName);
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

    for(const auto& mark : matchPairStart){
        int index = text.indexOf(mark.str);
        if(pos == -1 || index < pos || (index == pos && matchPairStartLength < mark.str.length())){
            pos = index;
            matchPairIndex = mark.matchPairIndex;
            matchPairStartLength = mark.str.length();
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

    struct Frame{
        QStringRef text;        //!< the text this frame deals with
        int lastPatternIndex;   //!< the pattern index current frame should not touch
        int curPatternIndex;    //!< current subpattern
        int advanceCount;       //!< how many characters is already consumed from the start of text
    };

    QStack<Frame> stack;
    stack.push({input, pattern.size(), 0, 0});

    // we always start from sub pattern zero

    while(true){
        // use a dedicated scope for sub pattern handling
        {
            Q_ASSERT(stack.size() > 0);
            Frame& f = stack.top();
            QStringRef text = f.text.mid(f.advanceCount);
            const SubPattern& p = pattern.at(f.curPatternIndex);
            switch(p.ty){
            case SubPatternType::Literal:{
                if(!text.startsWith(p.data)){
                    // match fail
                    return 0;
                }

                f.advanceCount += p.data.length();
                f.curPatternIndex += 1;
            }break;
            case SubPatternType::Regex:{
                auto result = p.regex.match(text, 0, QRegularExpression::NormalMatch, QRegularExpression::AnchoredMatchOption);
                if(!result.hasMatch()){
                    // match fail
                    return 0;
                }

                QStringList regexVarNames = p.regex.namedCaptureGroups();

                // the first capture should have no name
                Q_ASSERT(regexVarNames.front().isEmpty());

                for(int i = 1; i < regexVarNames.size(); ++i){
                    const QString& name = regexVarNames.at(i);
                    if(!name.isEmpty()){
                        values.insert(name, result.captured(i));
                    }
                }

                f.advanceCount += result.capturedRef(0).length();
                f.curPatternIndex += 1;
            }break;
            case SubPatternType::UpToTerminator:{
                int curAdvance = 0;
                while(true){
                    QStringRef advancedText = text.mid(curAdvance);
                    int index = -1;
                    if(!p.data.isEmpty()){
                        index = advancedText.indexOf(p.data);
                    }
                    if(index == -1){
                        // no terminator found
                        // however, if we are in match pair and this is the last subpattern, this is also a valid result
                        if(f.curPatternIndex + 1 == f.lastPatternIndex){
                            if(!p.valueName.isEmpty()){
                                // in this case we want to remove trailing substring in ignoreList
                                QStringRef curText = advancedText;
                                ctx.removeTrailingIgnoredString(curText);
                                values.insert(p.valueName, curText.toString());
                            }
                            // consumed everything
                            curAdvance += advancedText.length();
                            break;
                        }else{
                            // unexpected "terminator not found"
                            return 0;
                        }
                    }else{
                        // terminator found
                        // look for any match pair start mark before terminator
                        // if there is any, starting from the first such marker, find the matching end marker, then resume search from there
                        // we DO consume the terminator anyway, no matter whether it is included in variable or not
                        int startPos = -1;
                        int pairIndex = -1;
                        int startLength = -1;
                        int searchLength = index - 1 + ctx.longestMatchPairStartStringLength;
                        std::tie(startPos, pairIndex, startLength) = ctx.getMatchingStartAdvanceDistance(advancedText.left(searchLength));
                        if(startPos == -1 || index < startPos){
                            // no match pair start mark found, or they are not significant
                            // done
                            int terminatorInValueCount = 0;
                            int terminatorConsumeCount = 1;
                            switch(p.incMode){
                            case TerminatorInclusionMode::Avoid: break;
                            case TerminatorInclusionMode::IncludeOne:{
                                terminatorInValueCount = 1;
                            }break;
                            case TerminatorInclusionMode::IncludeSuccessive:{
                                while(advancedText.mid(index + terminatorConsumeCount * p.data.length()).startsWith(p.data.length())){
                                    terminatorConsumeCount += 1;
                                }
                                terminatorInValueCount = terminatorConsumeCount;
                            }break;
                            }
                            if(!p.valueName.isEmpty()){
                                values.insert(p.valueName, advancedText.mid(0, index + terminatorInValueCount * p.data.length()).toString());
                            }
                            curAdvance += index + terminatorConsumeCount * p.data.length();
                            break;
                        }else{
                            // match pair found
                            int pos = ctx.getMatchingEndAdvanceDistance(advancedText.mid(startPos + startLength), pairIndex).first;
                            if(pos == -1){
                                // broken expectation on match pair end
                                return 0;
                            }
                            curAdvance += startPos + startLength + pos;
                            continue;
                        }
                    } // end of else branch where terminator is found
                } // end of advance loop
                f.advanceCount += curAdvance;
                f.curPatternIndex += 1;
            }break;
            case SubPatternType::MatchPair:{
                // 1. find the match pair end
                // 2. adjust current frame so that now it jumps to next pattern outside match pair
                // 3. if there is any sub pattern inside match pair, push a frame so that they work on the text enclosed by the match pair
                //    otherwise, make sure everything enclosed by the match pair is in ignoreList, return fail if not the case
                // Note that the code here is responsible for skipping stuff in ignoreList in subtree enclosed by match pair
                int endAdvance = -1;
                int endMarkLen = -1;
                std::tie(endAdvance, endMarkLen) = ctx.getMatchingEndAdvanceDistance(text, p.matchPairIndex);
                if(endAdvance == -1){
                    // broken expectation on match pair end
                    return 0;
                }
                f.advanceCount += endAdvance;
                int nextPattern = f.curPatternIndex + 1;
                f.curPatternIndex = p.matchPairEndPatternIndex;

                QStringRef enclosedText = text.left(endAdvance - endMarkLen);

                ctx.removeLeadingIgnoredString(enclosedText);
                ctx.removeTrailingIgnoredString(enclosedText);

                if(nextPattern == p.matchPairEndPatternIndex){
                    // there is no sub patterns within this match pair
                    // if the enclosed text is not empty, report fail
                    if(!enclosedText.isEmpty()){
                        return 0;
                    }
                }else{
                    // there is sub patterns within this match pair
                    // report fail if there is no text left
                    if(enclosedText.isEmpty()){
                        return 0;
                    }
                    // prepare a new frame
                    Frame newFrame;
                    newFrame.text = enclosedText;
                    newFrame.lastPatternIndex = p.matchPairEndPatternIndex;
                    newFrame.curPatternIndex = nextPattern;
                    newFrame.advanceCount = 0;
                    stack.push(newFrame);
                }
            }break;
            }
        }
        // sub pattern handling done

        // check if it is the time to pop frame
        // if yes: if this is the last frame: return
        // otherwise, make sure everything left is in ignore list; otherwise it is a match failure
        // we may need to pop frames more than once
        while(true){
            Q_ASSERT(stack.size() > 0);
            Frame& f = stack.top();
            QStringRef text = f.text.mid(f.advanceCount);

            // step 1: remove ignored strings at beginning
            int trimLen = ctx.removeLeadingIgnoredString(text);
            f.advanceCount += trimLen;

            // step 2: check if it is time to pop frame
            if(f.curPatternIndex == f.lastPatternIndex){
                // it is okay for text to be non-empty if this is the last frame
                if(!text.isEmpty() && (stack.size() > 1)){
                    // ...otherwise it is a failure
                    return 0;
                }

                if(stack.size() == 1){
                    // this is the last frame
                    // just return
                    return f.advanceCount;
                }

                // okay, this frame is gone
                // continue checking on the next frame
                stack.pop();
                continue;
            }

            // not the time to pop frame
            // done
            break;
        }
    }
    Q_UNREACHABLE();
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
                    value.append(expr.data);
                }break;
                case PatternValueSubExpression::OpType::ValueReference:{
                    Q_ASSERT(rawValues.count(expr.data) > 0);
                    value.append(rawValues.value(expr.data));
                }break;
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
        std::function<QString(const QString &)> externReferenceSolver)
{
    if(valueTransform.isEmpty())
        return rawValues.value(paramName);

    QString value;
    for(const auto& expr : valueTransform){
        switch(expr.ty){
        case PatternValueSubExpression::OpType::Literal:{
            value.append(expr.data);
        }break;
        case PatternValueSubExpression::OpType::ValueReference:{
            if(rawValues.count(expr.data) > 0){
                value.append(rawValues.value(expr.data));
            }else{
                value.append(externReferenceSolver(expr.data));
            }
        }break;
        }
    }

    return value;
}
