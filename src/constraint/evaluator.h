#ifndef CONSTRAINT_EVALUATOR_H
#define CONSTRAINT_EVALUATOR_H

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <functional>
#include <ctime>
#include "../executor/variable_manager.h"

struct ConstraintResult {
    bool satisfied;
    std::string message;
    std::vector<std::string> failed_conditions;
    double confidence = 1.0;
};

enum class ConstraintType {
    EXPRESSION,
    TIME_WINDOW,
    RESOURCE,
    DEPENDENCY,
    STATE,
    SEMAPHORE
};

struct TimeWindowConstraint {
    std::string window_start;
    std::string window_end;
    std::string current_time_var = "current_time";
};

struct ResourceConstraint {
    std::string resource_id;
    std::string resource_type;
    int min_capacity = 1;
    int timeout_s = 0;
};

struct SemaphoreConstraint {
    std::string semaphore_id;
    int required_permits = 1;
    int timeout_s = 30;
    bool priority_enabled = false;
};

class ConstraintEvaluator {
public:
    explicit ConstraintEvaluator(VariableManager& var_mgr) 
        : var_mgr_(var_mgr), debug_enabled_(false) {}

    bool evaluate(const std::string& expression);
    ConstraintResult evaluateDetailed(const std::string& expression);

    bool evaluateTimeWindow(const TimeWindowConstraint& constraint);
    ConstraintResult evaluateTimeWindowDetailed(const TimeWindowConstraint& constraint);

    bool evaluateResource(const ResourceConstraint& constraint);
    ConstraintResult evaluateResourceDetailed(const ResourceConstraint& constraint);

    bool evaluateSemaphore(const SemaphoreConstraint& constraint);
    ConstraintResult evaluateSemaphoreDetailed(const SemaphoreConstraint& constraint);

    bool evaluateAll(const std::vector<std::string>& expressions);
    ConstraintResult evaluateAllDetailed(const std::vector<std::string>& expressions);

    void registerResourceChecker(std::function<bool(const ResourceConstraint&)> checker);
    void registerSemaphoreChecker(std::function<bool(const SemaphoreConstraint&)> checker);

    void enableDebug(bool enable) { debug_enabled_ = enable; }
    std::vector<std::string> getEvaluationLog() const { return evaluation_log_; }

    void clearLog() { evaluation_log_.clear(); }
    std::string substituteVariables(const std::string& expression);
    
private:
    VariableManager& var_mgr_;
    bool debug_enabled_;
    mutable std::vector<std::string> evaluation_log_;
    std::function<bool(const ResourceConstraint&)> resource_checker_;
    std::function<bool(const SemaphoreConstraint&)> semaphore_checker_;
    bool parseAndEvaluate(const std::string& expr);
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
    bool isOperator(const std::string& s) const;
    bool isLogicalOperator(const std::string& s) const;
    std::string trim(const std::string& s) const;
    std::time_t parseTimeString(const std::string& time_str) const;
    bool compareTime(const std::string& time1, const std::string& op, const std::string& time2) const;
};

#endif