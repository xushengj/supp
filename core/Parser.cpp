#include "core/Parser.h"

#include "core/IR.h"
#include "core/DiagnosticEmitter.h"

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
    //

    return nullptr;
}

IRRootInstance* Parser::parse(const QString& text, DiagnosticEmitterBase& diagnostic) const
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
    return nullptr;
}
