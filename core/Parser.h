#ifndef PARSER_H
#define PARSER_H

#include <QCoreApplication>
#include <QString>
#include <QList>
#include <QHash>
#include <QStringList>
#include <QStringRef>
#include <QRegularExpression>

#include <utility>

class DiagnosticEmitterBase;

class IRRootType;
class IRRootInstance;

struct ParserNode
{

    enum class NodeType{
        Leaf,           //!< Matching any pattern do not "push" current node to path
        InnerNode       //!< Matching the pattern cause current node to be "pushed" to path; next match will create node under current node
    };

    QString name;
    QStringList parameterNameList;  //!< parameters needed from patterns (all in string)

    struct Pattern{
        // we don't explicitly extract list of parameters/arguments; they are detected at validation time
        QString patternString;

        // following two string list should have the same name
        QStringList paramNameList;// name of parameters we want to overwrite
        QStringList paramValueList;// this is also a pattern; things enclosed by param match pair will be replaced with the value from pattern extraction
    };

    QList<Pattern> patterns;

    QString childNodeNameList;      //!< names of child ParserNode
    QStringList earlyExitPatterns;  //!< if the node can have children, finding anything inside this during matching children will pop path up to parent

    QString combineToNodeTypeName;  //!< the IRNodeType name that this node be converted to after the end of parsing
                                    //!< if this parser node has name matching with one from IR, this can be empty
                                    //!< this can also be empty if the node do not get lowered to IR (e.g. comments)

    QStringList combinedNodeParams; //!< expression list of node parameters (same format as elements in Pattern::paramValueList)
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
     * While the order of string in each equivalent set do not matter during matching,
     * The first string will always be considered as "canonical" marker.
     */
    struct MatchPairRecord{
        QStringList startEquivalentSet;
        QStringList endEquivalentSet;
    };

    QString name;

    QList<MatchPairRecord> matchPairs; //!< What should appear in pairs
    QStringList matchPairName; //!< for each match pair in matchPairs, what is the name

    QString parameterMatchPairName; //!< what's the name of match pair we use to enclose parameter names in match patterns

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
     * @param text the text input to pass in
     * @param ir the ir that this parser would generate. This must match with ir from getParser()
     * @param diagnostic the diagnostic where warnings / errors are reported to.
     * @return Pointer to the new'ed IR tree. The caller takes ownership of returned IR tree.
     */
    IRRootInstance* parse(const QString& text, const IRRootType& ir, DiagnosticEmitterBase& diagnostic) const;

private:
    // private constructor so that only getParser() can create instance
    Parser() = default;

private:

    struct ParseContext{
        struct MatchPairMark{
            QString str;
            int matchPairIndex;
        };

        QList<MatchPairMark> matchPairStart; // just a list of match pair mark; unordered
        QList<QStringList> matchPairEnds;
        QStringList matchPairName; // just for debugging purpose
        QStringList ignoreList;
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
    };

    /**
     * @brief The TerminatorInclusionMode enum determines how the terminator character is handled when a subtree finishes matching.
     *
     * Note that if the subtree is surrounded by a match pair, then the terminator would probably be nothing, in which case the mode is not used
     * Avoid: the subtree stops before the terminator.
     * IncludeOne: the subtree includes one instance of terminator.
     * IncludeSuccessive: the subtree includes all instances of terminator in the chain. ( "<text>." on "umm....." leaves text to "umm.....")
     */
    enum class TerminatorInclusionMode{
        Avoid,
        IncludeOne,
        IncludeSuccessive
    };

    enum class SubPatternType{
        Literal,        //!< the sub pattern match with a string literal
        Regex,          //!< the sub pattern match with a regular expression
        UpToTerminator, //!< the sub pattern match with everything up to a terminator
        MatchPair,      //!< the sub pattern is a tree that match a subtree enclosed by given match pair
    };
    struct SubPattern{
        QString data;           //!< for literal, this is the string literal to match (must not be empty)
                                //!< not used for regex
                                //!< for UpToTerminator, this is the terminator that ends the pattern (if empty, then match everything left)
                                //!< not used for MatchPair
        SubPatternType ty;      //!< the type of sub pattern

        // data for regex
        QRegularExpression regex;

        // data for UpToTerminator
        TerminatorInclusionMode incMode;
        QString valueName;      //!< what value this subpattern produce; empty if this value is discarded

        // data for MatchPair
        int matchPairEndPatternIndex;   //!< for MatchPair only, this is the index of first subpattern that is not part of current subpattern's subtree
        int matchPairIndex;             //!< the match pair index to match
    };
    /**
     * @brief The PatternValueTransformNode struct describes a sub expression for parser node value transformation
     */
    struct PatternValueSubExpression{
        enum class OpType{
            Literal,
            ValueReference
        };
        OpType ty;
        QString data;
    };
    struct Pattern{
        QList<SubPattern> elements;
        QList<QList<PatternValueSubExpression>> valueTransform;//!< for each parameter of parser node (outer index), how is the final value made by concatenating sub expressions
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
     * @return list of parameter values
     */
    static QStringList performValueTransform(const QStringList& paramName, const QHash<QString,QString>& rawValues, const QList<QList<PatternValueSubExpression>>& valueTransform);

    /**
     * @brief findLongestMatchingPattern tries all listed pattern on beginning of text and find the one that consumes more characters than others
     * @param patterns the list of patterns to try on
     * @param text input text
     * @param values the raw values retrieved when doing the matching
     * @return pair of <pattern index, # characters consumed>; <-1,0> if no pattern applies
     */
    std::pair<int,int> findLongestMatchingPattern(const QList<Pattern>& patterns, QStringRef text, QHash<QString,QString>& values) const;

    struct Node{
        QString nodeName;
        QList<Pattern> patterns;
        QList<Pattern> earlyExitPatterns;
        QStringList paramName;
        QString combineToIRNodeTypeName;
        QList<QList<PatternValueSubExpression>> combineValueTransform;
        QList<int> allowedChildNodeIndexList;
    };

    // node 0 is the root node; if the root node has patterns, it must be matched first, otherwise the root node implicitly starts
    QList<Node> nodes;
    ParseContext context;
};

#endif // PARSER_H
