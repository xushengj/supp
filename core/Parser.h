#ifndef PARSER_H
#define PARSER_H

#include <QCoreApplication>
#include <QString>
#include <QList>
#include <QHash>
#include <QSet>
#include <QStringList>
#include <QStringRef>
#include <QRegularExpression>
#include <QVector>

#include <functional>
#include <utility>

class DiagnosticEmitterBase;

class IRRootType;
class IRRootInstance;

struct ParserNode
{
    QString name;
    QStringList parameterNameList;  //!< parameters needed from patterns (all in string)

    struct Pattern{
        // we don't explicitly extract list of parameters/arguments; they are detected at validation time
        QString patternString;      //!< the pattern, specified as string
                                    //!< first of all, all substrings in ignore list are ignored
                                    //!< if a match pair starts:
                                    //!<    if the match pair is parameter match pair, interpret the enclosed content as follows:
                                    //!<    assume the param match pair is <>: (note that characters inside <> should be exact match)
                                    //!<        regex:
                                    //!<            <[regex](**"..."**)> (C++ raw string literal style enclosure with '(',')' and '"' reversed)
                                    //!<            <[regex]"..."> (direct quote; first '"' is the end)
                                    //!<        literal:
                                    //!<            <(**"..."**)>
                                    //!<            <"...">
                                    //!<            This form allows literals to include something in ignore list and make them mandatory for matching
                                    //!<        Auto:
                                    //!<            No next sub pattern inclusion: <MyParam>.
                                    //!<            Include first character from the next literal: <MyParam+>.
                                    //!<            Include entire next sub pattern: <MyParam+*>.
                                    //!<
                                    //!<    otherwise, start a hierarchy with MatchPair
                                    //!< otherwise, get it as literal

        int priorityScore;  //!< priority score override; set to 0 if it should be auto computed.

        /**
         * @brief The ParamValueOverwriteRecord struct describes an overwrite on ParserNode parameter
         *
         * If a pattern do not have fields for a parameter, if the user wants to initialize the parameter, there needs to be an overwrite record for that
         */
        struct ParamValueOverwriteRecord{
            QString paramName;  //!< the name of parameter to overwrite
            QString valueExpr;  //!< the value expression used for overwriting. Cannot reference any variables outside current ParserNode.
                                //!< Assume parameter match pair is "<" and ">":
                                //!< explicit literal: <(**"..."**)>, <"..."> (only useful for including parameter match pair strings inside final value)
                                //!< value reference: <Expr> (Expr is guaranteed not starting with '(' or '"')
        };

        QList<ParamValueOverwriteRecord> valueOverwriteList;
    };

    QList<Pattern> patterns;

    QStringList childNodeNameList;  //!< names of child ParserNode
    QStringList earlyExitPatterns;  //!< if the node can have children, finding anything inside this during matching children will pop path up to parent

    QString combineToNodeTypeName;  //!< the IRNodeType name that this node be converted to after the end of parsing
                                    //!< if this parser node has name matching with one from IR, this can be empty
                                    //!< this can also be empty if the node do not get lowered to IR (e.g. comments)

    QHash<QString, QStringList> combinedNodeParams; //!< for each IR node parameter,
                                                    //!< what list of expression to try for it
                                                    //!< final result will be from the first successful evaluation
};

struct ParserPolicy
{
    /**
     * @brief The MatchPairRecord struct describe strings that should appear in pairs
     *
     * For paired characters like quotation "" and parenthesis (), we want to match them in pairs
     * Each MatchPairRecord describes what should appear in pairs
     * Each equivalent set describes all equivalent strings that can be used as the mark of the match pair
     * For example:
     *     match quotation mark: start = {"\""}, end = {"\""}
     *     match parenthesis: start = {"("}, end = {")"}
     *     match string "begin"/"begins" and "end"/"ends": start = {"begin", "begins"}, end = {"end", "ends"}
     * During "expansion" of parameter/subtree, When any string in a start set is encountered,
     * the parser will jump to the end of matching end marker, then looking for end of subtree.
     * The order of string in each equivalent set do not matter during matching.
     */
    struct MatchPairRecord{
        QString name;
        QStringList startEquivalentSet;
        QStringList endEquivalentSet;
    };

    QString name;

    QList<MatchPairRecord> matchPairs; //!< What should appear in pairs

    // what do patterns and value transform expressions use to enclose something that is not a literal
    // for minimal surprises the match pair for parameters must only have 1 string each for start and end marker
    QString exprStartMark;
    QString exprEndMark;

    QStringList ignoreList; //!< any string appear in ignoreList will be ignored before testing other patterns (whitespace, etc)

    QList<ParserNode> nodes;
    QString rootParserNodeName;
};

class Parser
{
    Q_DECLARE_TR_FUNCTIONS(Parser)
public:
    /**
     * @brief getParser get an instance of parser. return nullptr if parser is not valid
     * @param policy the ParserPolicy taking effect
     * @param ir the IR the parser will be generating
     * @param diagnostic the diagnostic for validating ParserPolicy
     * @return parser instance. Caller takes the ownership. nullptr if validation fails.
     */
    static Parser* getParser(const ParserPolicy& policy, const IRRootType& ir, DiagnosticEmitterBase& diagnostic);

    /**
     * @brief parse parse the text input and produce the IR tree instance.
     * @param text the text input to pass in. Each item is a text unit (where no pattern can match across unit boundary).
     * @param ir the ir that this parser would generate. This must match with ir from getParser()
     * @param diagnostic the diagnostic where warnings / errors are reported to.
     * @return Pointer to the new'ed IR tree. The caller takes ownership of returned IR tree.
     */
    IRRootInstance* parse(QVector<QStringRef>& text, const IRRootType& ir, DiagnosticEmitterBase& diagnostic) const;

private:
    // private constructor so that only getParser() can create instance
    Parser() = default;

private:
    /**
     * @brief The TerminatorInclusionMode enum determines how the terminator character is handled when an Auto type sub pattern ends.
     */
    enum class TerminatorInclusionMode{
        NoTerminator,       //!< the sub pattern is the last in a subtree; no terminator for it
        Avoid,              //!< the sub pattern should stop before the terminator; no text for it is consumed.
        IncludeOne,         //!< the sub pattern includes one instance of terminator.
        IncludeSuccessive   //!< the sub pattern includes all instances of terminator in the chain. ( "<text>." on "umm....." leaves text to "umm.....")
    };

    /*
     * Note on auto mode:
     * Sub pattern with "Auto" type will generally peek next sub pattern to decide how much text to match
     * If the next sub pattern is also an Auto type sub pattern, then any substring in ignore list can finish this pattern
     * Otherwise, this pattern match until first match of next sub pattern
     * If there is no "next sub pattern", it matches until the end of text unit
     * Auto sub pattern can optionally include content of the next sub pattern,
     *      if there is a "next sub pattern" and the next sub pattern is not another sub pattern
     * In all cases, the matched content will be trimmed so that it do not start / end with a string in ignore list
     */
    enum class SubPatternType{
        Literal,        //!< the sub pattern match with a string literal
        Regex,          //!< the sub pattern match with a regular expression
        Auto,           //!< the sub pattern match with everything up to next sub pattern. Boundary is automatically detected.
        MatchPair,      //!< the sub pattern match with a match pair start or end marker
    };
    struct SubPattern{
        SubPatternType ty = SubPatternType::Literal;    //!< the type of sub pattern

        struct LiteralData{
            QString str;
        };
        struct RegexData{
            QRegularExpression regex;
        };
        struct AutoData{
            QString valueName;
            bool isTerminateByIgnoredString = false;    //!< set to true if there is no "next sub pattern"
            int nextSubPatternIncludeLength = 0;        //!< how many characters from the next sub pattern is included in this pattern; -1 for including the entire match
        };
        struct MatchPairData{
            int matchPairIndex = -1;            //!< the match pair index to match
            bool isStart = true;                //!< whether the match pair
        };

        LiteralData literalData;
        RegexData regexData;
        AutoData autoData;
        MatchPairData matchPairData;
    };
    /**
     * @brief The PatternValueTransformNode struct describes a sub expression for parser node value transformation
     */
    struct PatternValueSubExpression{
        enum class OpType{
            Literal,
            LocalReference,
            ExternReference
        };
        OpType ty;
        struct LiteralData{
            QString str;
        };
        struct LocalReferenceData{
            QString valueName;
        };

        /**
         * @brief The ExternReferenceData struct describes a non-local reference, made by a node traversal path and parameter name pair
         *
         * each reference should be in the form <NodeExpr>.<ParamName>
         * no other expression allowed
         * <NodeExpr>:
         *      parent node: ..
         *      child node:
         *          <ChildNodeName>                 (for accessing the only child)
         *          <ChildNodeName>[<LookupExpr>]   (for accessing child in that type or for lookup)
         *          [<index expr>]                  (for accessing child by order / occurrence)
         *
         *      <LookupExpr>:
         *                pure number (no +/-): index (0 based) of child node in given type
         *                <ParamName>=="<ParamValue>": key based lookup
         *                (+/-)pure number: (only when accessing ../<Child>[+/-offset]) index based search; 0 is the last node with given type BEFORE or IS current node
         *       <index expr>:
         *                pure number (no +/-): index (0 based) of node in given parent, no matter which type
         *                (+/-)pure number:     offset of node in given parent (index = <index of this node under parent> + offset)
         *
         * chaining node reference: use '/', e.g. ../../Child1[0]/Child2[Key="Value"]
         */
        struct ExternReferenceData{
            struct NodeTraverseStep{
                enum class StepType{
                    Parent,
                    ChildByTypeAndOrder,
                    ChildByTypeFromLookup,
                    AnyChildByOrder
                };
                struct IndexOrderSearchData{
                    int lookupNum = 0;
                    bool isNumIndexInsteadofOffset = false;
                };
                struct KeyValueSearchData{
                    QString key;
                    QString value;
                };

                StepType ty = StepType::Parent;
                QString childParserNodeName;
                IndexOrderSearchData ioSearchData;
                KeyValueSearchData kvSearchData;
            };
            QList<NodeTraverseStep> nodeTraversal;
            bool isTraverseStartFromRoot = false;
            QString valueName;
        };
        LiteralData literalData;
        LocalReferenceData localReferenceData;
        ExternReferenceData externReferenceData;
    };
    struct Pattern{
        QList<SubPattern> elements;
        QList<QList<PatternValueSubExpression>> valueTransform;//!< for each parameter of parser node (outer index),
                                                               //!< how is the final value made by concatenating sub expressions
        int priorityScore;  //!< a score calculated based on complexity of pattern
                            //!< (number of sub patterns, length of literals, etc)
                            //!< A pattern with higher score is chosen if it matches same length of text with other patterns
    };

    struct ParseContext{
        QList<QStringList> matchPairStarts;
        QList<QStringList> matchPairEnds;
        QStringList matchPairName; // just for debugging purpose
        QStringList ignoreList;
        QString exprStartMark; // exprStartMark and exprEndMark is not used after parser is initialized
        QString exprEndMark;
        int longestMatchPairStartStringLength;

        /**
         * @brief getMatchingEndAdvanceDistance returns how many characters to skip so that one jumps to the first character after the end mark of matching match pair
         * @param text the text to search on. The start marker should already be consumed.
         * @param matchPairIndex the match pair index for skipping.
         * @return pair of <number of characters to skip (-1 if no match found), length of match pair end string>
         */
        std::pair<int,int> getMatchingEndAdvanceDistance(QStringRef text, int matchPairIndex)const;


        /**
         * @brief getMatchingStartAdvanceDistance finds the first match pair start mark and return its pos within text
         * @param text the text to search on. The start marker should not be consumed.
         * @param matchPairIndex the index of match pair; will be written for result
         * @param matchPairStartLength the length of match pair start string
         * @return tuple of <position or advance distance (-1 if none found), match pair index, match pair start length>
         */
        std::tuple<int,int,int> getMatchingStartAdvanceDistance(QStringRef text)const;

        int removeTrailingIgnoredString(QStringRef& ref)const;
        int removeLeadingIgnoredString(QStringRef& ref)const;

        /**
         * @brief parsePatternString convert pattern string in ParserNode::Pattern style to Parser::Pattern
         *
         * Caller should make sure that all containers are cleared before the function is called.
         * If parse fails, containers may contain arbitrary garbage value; caller should backup data if needed.
         *
         * @param pattern the source pattern string to convert
         * @param result the result pattern
         * @param valueNameToIndex the result [ValueName]->[SubPatternIndex] mapping
         * @param diagnostic destination of diagnostic from environment
         * @return true if the string is parsed successfully, false otherwise
         */
        bool parsePatternString(const QString& pattern, QList<SubPattern>& result, QHash<QString,int>& valueNameToIndex, DiagnosticEmitterBase &diagnostic);

        /**
         * @brief parseValueTransformString convert value transform string in ParserNode::Pattern style to Parser::Pattern format
         *
         * Caller should make sure that all containers are cleared before the function is called.
         * If parse fails, containers may contain arbitrary garbage value; caller should backup data if needed.
         *
         * @param transform the source transform string to convert
         * @param result the result transform
         * @param referencedValues which local values are referenced by this transform; this function only inserts to it
         * @param isLocalOnly whether value transform is allowed to reference values outside current node
         * @param diagnostic destination of diagnostic from environment
         * @return true if the string is parsed successfully, false otherwise
         */
         bool parseValueTransformString(const QString& transform, QList<PatternValueSubExpression>& result, QSet<QString>& referencedValues, bool isLocalOnly, DiagnosticEmitterBase &diagnostic);
    };

    /**
     * @brief match perform matching on given input text
     * @param input the text input
     * @param values the values for write back
     * @return number of characters this pattern can consume. 0 if pattern does not apply
     */
    static int match(QStringRef input, QHash<QString,QString>& values, const ParseContext& ctx, const QList<SubPattern>& pattern);

    /**
     * @brief performValueTransform is for transforming the parsed raw value to parser node parameters
     * @param paramName list of parameter names
     * @param rawValues the raw values populated by match()
     * @param valueTransform describes how each parameters get their value from rawValues
     * @return list of parameter values
     */
    static QStringList performValueTransform(
            const QStringList& paramName,
            const QHash<QString,QString>& rawValues,
            const QList<QList<PatternValueSubExpression>>& valueTransform
    );

    /**
     * @brief performValueTransform variant that take an expression solver callback. This must only work with one parameter at a time.
     * @param paramName the name of parameter
     * @param rawValues the raw values populated by match()
     * @param valueTransform describes how the parameter get its value from rawValues
     * @param externReferenceSolver used to resolve variable references that are not in rawValues
     * @return
     */
    static QString performValueTransform(const QString& paramName,
            const QHash<QString,QString>& rawValues,
            const QList<PatternValueSubExpression>& valueTransform,
            std::function<QString(const PatternValueSubExpression::ExternReferenceData &)> externReferenceSolver
    );

    /**
     * @brief findLongestMatchingPattern tries all listed pattern on beginning of text and find the one that consumes more characters than others
     * @param patterns the list of patterns to try on
     * @param text input text
     * @param values the raw values retrieved when doing the matching
     * @return pair of <pattern index, # characters consumed>; <-1,0> if no pattern applies
     */
    std::pair<int,int> findLongestMatchingPattern(const QList<Pattern>& patterns, QStringRef text, QHash<QString,QString>& values) const;

    static int computePatternScore(const QList<SubPattern>& pattern, const QList<int>& matchPairScore);

    /**
     * @brief The ParserNodeData struct contains record of a parser node generated during pattern matching
     */
    struct ParserNodeData{
        int nodeTypeIndex;      //!< index in Parser::nodes
        int parentIndex;        //!< parent parser node in the list containning this item
        int indexWithinParent;  //!< index of node within parent
        int childNodeCount;     //!< How many child node do this node has
        QStringList params;
    };
    QList<ParserNodeData> patternMatch(QVector<QStringRef>& text, DiagnosticEmitterBase& diagnostic) const;

    struct IRBuildContext{
        QList<ParserNodeData> parserNodes;
        QHash<int,QHash<int,QList<int>>> parserNodeChildListCache;//!< [ParserNodeIndex][ChildParserNodeTypeIndex] -> [list of child node index in parserNodes]


        /**
         * @brief getNodeChildList helper function to access parserNodeChildListCache; build the list if not found in cache
         * @param parentIndex parent index where list of child is needed
         * @param childNodeTypeIndex child parser node type index; -1 if all child is needed
         * @return reference to child node index (in parserNodes) list
         */
        const QList<int>& getNodeChildList(int parentIndex, int childNodeTypeIndex);

        /**
         * @brief solveExternReference helper function for solve extern variable reference from parser node specified by nodeIndex
         * @param p the Parser parent
         * @param expr the extern variable reference expression
         * @param nodeIndex the parser node index in parserNodes where the expr is evaluated
         * @return pair of <isGood, result string>
         */
        std::pair<bool,QString> solveExternReference(const Parser &p, const PatternValueSubExpression::ExternReferenceData& expr, int nodeIndex);
    };

private:
    // actual data

    struct Node{
        QString nodeName;
        QList<Pattern> patterns;            //!< nodes with no pattern starts implicitly, i.e. when child node pattern matches.
                                            //!< These nodes should not have parameters
        QList<Pattern> earlyExitPatterns;
        QStringList paramName;

        int combineToIRNodeIndex;           //!< the IRNode index to tranform to; -1 if this is a parser-only node

        QList<QList<QList<PatternValueSubExpression>>> combineValueTransform;   //!< [ParamIndex][ExprIndex][List of sub expressions]
                                                                                //!< for combine value transform, for each parameter, we allow multiple expression
                                                                                //!< the result of first successful evaluation is used as final result
        QList<int> allowedChildNodeIndexList;
    };

    // node 0 is the root node; if the root node has patterns, it must be matched first, otherwise the root node implicitly starts
    QList<Node> nodes;
    ParseContext context;
};

#endif // PARSER_H
