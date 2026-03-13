#ifndef CONSTRAINT_EVALUATOR_H
#define CONSTRAINT_EVALUATOR_H

#include <string>
#include <vector>
#include <map>
#include "../executor/variable_manager.h"

struct ConstraintResult {
    bool satisfied;
    std::string message;
    std::vector<std::string> failed_conditions;
    double confidence = 1.0;
};

class ConstraintEvaluator {
public:
    explicit ConstraintEvaluator(VariableManager& var_mgr) 
        : var_mgr_(var_mgr), debug_enabled_(false) {}

    bool evaluate(const std::string& expression);
    ConstraintResult evaluateDetailed(const std::string& expression);

    void enableDebug(bool enable) { debug_enabled_ = enable; }
    
private:
    VariableManager& var_mgr_;
    bool debug_enabled_;
    enum class TokenType { 
        NUMBER,
        STRING,
        IDENTIFIER,
        OPERATOR,
        LPAREN,
        RPAREN,
        AND,
        OR,
        NOT,
        END
    };
    struct Token {
        TokenType type;
        std::string value;
        Token(TokenType t, const std::string& v = "") : type(t), value(v) {}
    };
    std::vector<Token> tokenize(const std::string& expr);
    bool parseExpression(const std::vector<Token>& tokens, size_t& pos);
    bool parseOrExpression(const std::vector<Token>& tokens, size_t& pos);
    bool parseAndExpression(const std::vector<Token>& tokens, size_t& pos);
    bool parseNotExpression(const std::vector<Token>& tokens, size_t& pos);
    bool parseComparison(const std::vector<Token>& tokens, size_t& pos);
    bool parsePrimary(const std::vector<Token>& tokens, size_t& pos);
    bool compare(const VariableValue& left, const std::string& op, const VariableValue& right);
    void log(const std::string& message) const;
    std::string trim(const std::string& s) const;
};

#endif