#ifndef PARSER_H
#define PARSER_H

#include <QCoreApplication>
#include <QString>
#include <QList>
#include <QStringList>

class DiagnosticEmitterBase;

class IRRootType;
class IRRootInstance;

struct ParserNode
{

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

    QStringList ignoreList; //!< any patterns appear in ignoreList will be ignored before testing other patterns (for comments, whitespace, etc)

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
     * @param diagnostic the diagnostic where warnings / errors are reported to.
     * @return Pointer to the new'ed IR tree. The caller takes ownership of returned IR tree.
     */
    IRRootInstance* parse(const QString& text, DiagnosticEmitterBase& diagnostic) const;

private:
    Parser(const ParserPolicy& policy, const IRRootType& ir)
        : policy(policy), ir(ir)
    {}

    const ParserPolicy& policy;
    const IRRootType& ir;
};

#endif // PARSER_H
