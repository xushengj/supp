#include "core/Parser.h"

#include "core/IR.h"
#include "core/DiagnosticEmitter.h"

#include <QRegularExpressionMatch>
#include <QStack>
#include <QStringRef>

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

IRRootInstance* Parser::parse(const QString& text, const IRRootType& ir, DiagnosticEmitterBase& diagnostic) const
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
    QList<QStringList> parserNodeParams; // [parser node index] -> list of parser node params
    struct ParserNodeData{
        int nodeTypeIndex;  //!< index in Parser::nodes
        int parentIndex;    //!< parent parser node in parserNodes (local variable)
        QStringList params;
    };
    QList<ParserNodeData> parserNodes;
    int currentParentIndex = -1;

    // seek through first set of ignored characters
    QStringRef in(&text, 0, text.length());
    context.removeLeadingIgnoredString(in);

    // bootstrap: starting the root node
    {
        const auto& root = nodes.at(0);
        ParserNodeData firstNode;
        firstNode.nodeTypeIndex = 0;
        firstNode.parentIndex   = -1;
        if(root.patterns.empty()){
            // if there is no pattern in root parser node, it is implicitly started
            Q_ASSERT(root.paramName.empty());
        }else{
            QHash<QString,QString> values;
            int patternIndex = -1;
            int advanceDist = 0;
            std::tie(patternIndex, advanceDist) = findLongestMatchingPattern(root.patterns, in, values);

            if(Q_UNLIKELY(patternIndex == -1)){
                // cannot start on root
                return nullptr;
            }

            firstNode.params = performValueTransform(root.paramName, values, root.patterns.at(patternIndex).valueTransform);
        }
        parserNodes.push_back(firstNode);
        currentParentIndex = 0;
    }

    // TODO

    return nullptr;
}

std::pair<int,int> Parser::findLongestMatchingPattern(const QList<Pattern>& patterns, QStringRef text, QHash<QString,QString>& values) const
{
    int bestPatternIndex = -1;
    int numConsumed = 0;
    QHash<QString, QString> curRawValue;

    for(int i = 0, num = patterns.size(); i < num; ++i){
        int curConsumeCount = match(text, curRawValue, context, patterns.at(i).elements);
        if(curConsumeCount > numConsumed){
            bestPatternIndex = i;
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
                    value.append(rawValues.value(expr.data));
                }break;
                }
            }
        }
        result.push_back(value);
    }

    return result;
}
