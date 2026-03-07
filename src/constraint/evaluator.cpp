#include "evaluator.h"
#include <sstream>
#include <algorithm>
#include <cctype>
#include <ctime>
#include <iomanip>

bool ConstraintEvaluator::evaluate(const std::string& expression) {
    return evaluateDetailed(expression).satisfied;
}

ConstraintResult ConstraintEvaluator::evaluateDetailed(const std::string& expression) {
    ConstraintResult result;
    result.satisfied = false;
    result.confidence = 1.0;   
    try {
        log("Evaluating: " + expression);
        std::vector<Token> tokens = tokenize(expression);
        if (tokens.empty() || (tokens.size() == 1 && tokens[0].type == TokenType::END)) {
            result.message = "Empty expression";
            result.failed_conditions.push_back(expression);
            return result;
        }
        size_t pos = 0;
        result.satisfied = parseExpression(tokens, pos);
        if (result.satisfied) {
            result.message = "All constraints satisfied";
        } else {
            result.message = "Constraint not satisfied: " + expression;
            result.failed_conditions.push_back(expression);
        }
        log("Result: " + std::string(result.satisfied ? "PASS" : "FAIL"));
    } catch (const std::exception& e) {
        result.satisfied = false;
        result.message = "Evaluation error: " + std::string(e.what());
        result.failed_conditions.push_back(expression);
        log("Error: " + result.message);
    }
    return result;
}

bool ConstraintEvaluator::evaluateTimeWindow(const TimeWindowConstraint& constraint) {
    return evaluateTimeWindowDetailed(constraint).satisfied;
}

ConstraintResult ConstraintEvaluator::evaluateTimeWindowDetailed(const TimeWindowConstraint& constraint) {
    ConstraintResult result;
    result.satisfied = false;
    try {
        std::string current_time;
        if (var_mgr_.exists(constraint.current_time_var)) {
            current_time = var_mgr_.get(constraint.current_time_var).asString();
        } else {
            std::time_t now = std::time(nullptr);
            char buf[32];
            std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", std::gmtime(&now));
            current_time = buf;
        }
        bool after_start = compareTime(current_time, ">=", constraint.window_start);
        bool before_end = compareTime(current_time, "<=", constraint.window_end);
        result.satisfied = after_start && before_end;
        if (result.satisfied) {
            result.message = "Time window constraint satisfied";
        } else {
            result.message = "Time window constraint failed: current=" + current_time + 
                           ", window=[" + constraint.window_start + " to " + constraint.window_end + "]";
            result.failed_conditions.push_back("time_window");
        }
        log("Time window: " + result.message);
    } catch (const std::exception& e) {
        result.satisfied = false;
        result.message = "Time window evaluation error: " + std::string(e.what());
        result.failed_conditions.push_back("time_window");
    }
    return result;
}

bool ConstraintEvaluator::evaluateResource(const ResourceConstraint& constraint) {
    return evaluateResourceDetailed(constraint).satisfied;
}

ConstraintResult ConstraintEvaluator::evaluateResourceDetailed(const ResourceConstraint& constraint) {
    ConstraintResult result;
    result.satisfied = false;
    try {
        if (resource_checker_) {
            result.satisfied = resource_checker_(constraint);
            result.message = result.satisfied ? 
                "Resource available: " + constraint.resource_id : 
                "Resource unavailable: " + constraint.resource_id;
        } else {
            std::string var_name = "resource_" + constraint.resource_id + "_available";
            if (var_mgr_.exists(var_name)) {
                int available = var_mgr_.get(var_name).asInt();
                result.satisfied = (available >= constraint.min_capacity);
                result.message = "Resource " + constraint.resource_id + 
                               " available: " + std::to_string(available) + 
                               "/" + std::to_string(constraint.min_capacity);
            } else {
                result.satisfied = false;
                result.message = "Resource status unknown: " + constraint.resource_id;
                result.confidence = 0.5;
            }
        }
        if (!result.satisfied) {
            result.failed_conditions.push_back("resource_" + constraint.resource_id);
        }
        log("Resource constraint: " + result.message);
    } catch (const std::exception& e) {
        result.satisfied = false;
        result.message = "Resource evaluation error: " + std::string(e.what());
        result.failed_conditions.push_back("resource_" + constraint.resource_id);
    }
    return result;
}

bool ConstraintEvaluator::evaluateSemaphore(const SemaphoreConstraint& constraint) {
    return evaluateSemaphoreDetailed(constraint).satisfied;
}

ConstraintResult ConstraintEvaluator::evaluateSemaphoreDetailed(const SemaphoreConstraint& constraint) {
    ConstraintResult result;
    result.satisfied = false;
    try {
        if (semaphore_checker_) {
            result.satisfied = semaphore_checker_(constraint);
            result.message = result.satisfied ? 
                "Semaphore available: " + constraint.semaphore_id : 
                "Semaphore unavailable: " + constraint.semaphore_id;
        } else {
            std::string permits_var = "sem_" + constraint.semaphore_id + "_available_permits";    
            if (var_mgr_.exists(permits_var)) {
                int available = var_mgr_.get(permits_var).asInt();
                result.satisfied = (available >= constraint.required_permits);
                result.message = "Semaphore " + constraint.semaphore_id + 
                               " permits: " + std::to_string(available) + 
                               "/" + std::to_string(constraint.required_permits);
            } else {
                result.satisfied = false;
                result.message = "Semaphore status unknown: " + constraint.semaphore_id;
                result.confidence = 0.5;
            }
        }
        if (!result.satisfied) {
            result.failed_conditions.push_back("semaphore_" + constraint.semaphore_id);
        }
        log("Semaphore constraint: " + result.message); 
    } catch (const std::exception& e) {
        result.satisfied = false;
        result.message = "Semaphore evaluation error: " + std::string(e.what());
        result.failed_conditions.push_back("semaphore_" + constraint.semaphore_id);
    }
    return result;
}

bool ConstraintEvaluator::evaluateAll(const std::vector<std::string>& expressions) {
    return evaluateAllDetailed(expressions).satisfied;
}

ConstraintResult ConstraintEvaluator::evaluateAllDetailed(const std::vector<std::string>& expressions) {
    ConstraintResult result;
    result.satisfied = true;
    result.confidence = 1.0;
    for (const auto& expr : expressions) {
        ConstraintResult sub_result = evaluateDetailed(expr);
        if (!sub_result.satisfied) {
            result.satisfied = false;
            result.failed_conditions.insert(result.failed_conditions.end(), 
                                           sub_result.failed_conditions.begin(),
                                           sub_result.failed_conditions.end());
        }
        result.confidence = std::min(result.confidence, sub_result.confidence);
    }
    if (result.satisfied) {
        result.message = "All " + std::to_string(expressions.size()) + " constraints satisfied";
    } else {
        result.message = std::to_string(result.failed_conditions.size()) + 
                        " of " + std::to_string(expressions.size()) + " constraints failed";
    }
    return result;
}

void ConstraintEvaluator::registerResourceChecker(std::function<bool(const ResourceConstraint&)> checker) {
    resource_checker_ = checker;
}

void ConstraintEvaluator::registerSemaphoreChecker(std::function<bool(const SemaphoreConstraint&)> checker) {
    semaphore_checker_ = checker;
}

std::string ConstraintEvaluator::substituteVariables(const std::string& expression) {
    std::string result = expression;
    size_t pos = 0;
    while ((pos = result.find("${", pos)) != std::string::npos) {
        size_t end = result.find("}", pos);
        if (end == std::string::npos) break;
        std::string var_name = result.substr(pos + 2, end - pos - 2);   
        try {
            if (var_mgr_.exists(var_name)) {
                std::string value = var_mgr_.get(var_name).asString();
                result.replace(pos, end - pos + 1, value);
                pos += value.length();
            } else {
                pos = end + 1;
            }
        } catch (...) {
            pos = end + 1;
        }
    }
    return result;
}

std::vector<ConstraintEvaluator::Token> ConstraintEvaluator::tokenize(const std::string& expr) {
    std::vector<Token> tokens;
    std::string trimmed = trim(expr);
    size_t i = 0;
    while (i < trimmed.length()) {
        char c = trimmed[i];  
        if (std::isspace(c)) {
            ++i;
            continue;
        }
        if (c == '(') {
            tokens.push_back(Token(TokenType::LPAREN, "("));
            ++i;
            continue;
        }
        if (c == ')') {
            tokens.push_back(Token(TokenType::RPAREN, ")"));
            ++i;
            continue;
        }
        if (c == '\'' || c == '\"') {
            char quote = c;
            size_t start = i + 1;
            size_t end = trimmed.find(quote, start);
            if (end != std::string::npos) {
                tokens.push_back(Token(TokenType::STRING, trimmed.substr(start, end - start)));
                i = end + 1;
                continue;
            }
        }
        if (std::isdigit(c) || (c == '-' && i + 1 < trimmed.length() && std::isdigit(trimmed[i + 1]))) {
            size_t start = i;
            if (c == '-') ++i;
            while (i < trimmed.length() && (std::isdigit(trimmed[i]) || trimmed[i] == '.')) {
                ++i;
            }
            tokens.push_back(Token(TokenType::NUMBER, trimmed.substr(start, i - start)));
            continue;
        }
        if (i + 1 < trimmed.length()) {
            std::string two_char = trimmed.substr(i, 2);
            if (two_char == "&&") {
                tokens.push_back(Token(TokenType::AND, "&&"));
                i += 2;
                continue;
            }
            if (two_char == "||") {
                tokens.push_back(Token(TokenType::OR, "||"));
                i += 2;
                continue;
            }
            if (two_char == "==" || two_char == "!=" || two_char == "<=" || 
                two_char == ">=" || two_char == "<<" || two_char == ">>") {
                tokens.push_back(Token(TokenType::OPERATOR, two_char));
                i += 2;
                continue;
            }
        } 
        if (c == '<' || c == '>' || c == '=' || c == '!') {
            tokens.push_back(Token(TokenType::OPERATOR, std::string(1, c)));
            ++i;
            continue;
        }
        if (c == '!') {
            tokens.push_back(Token(TokenType::NOT, "!"));
            ++i;
            continue;
        }
        if (std::isalpha(c) || c == '_') {
            size_t start = i;
            while (i < trimmed.length() && (std::isalnum(trimmed[i]) || trimmed[i] == '_')) {
                ++i;
            }
            std::string word = trimmed.substr(start, i - start);
            if (word == "and" || word == "AND") {
                tokens.push_back(Token(TokenType::AND, "&&"));
            } else if (word == "or" || word == "OR") {
                tokens.push_back(Token(TokenType::OR, "||"));
            } else if (word == "not" || word == "NOT") {
                tokens.push_back(Token(TokenType::NOT, "!"));
            } else if (word == "true" || word == "TRUE" || word == "True") {
                tokens.push_back(Token(TokenType::NUMBER, "1"));
            } else if (word == "false" || word == "FALSE" || word == "False") {
                tokens.push_back(Token(TokenType::NUMBER, "0"));
            } else {
                tokens.push_back(Token(TokenType::IDENTIFIER, word));
            }
            continue;
        } 
        ++i;
    }
    tokens.push_back(Token(TokenType::END));
    return tokens;
}

bool ConstraintEvaluator::parseExpression(const std::vector<Token>& tokens, size_t& pos) {
    return parseOrExpression(tokens, pos);
}

bool ConstraintEvaluator::parseOrExpression(const std::vector<Token>& tokens, size_t& pos) {
    bool result = parseAndExpression(tokens, pos);
    while (pos < tokens.size() && tokens[pos].type == TokenType::OR) {
        ++pos;
        bool right = parseAndExpression(tokens, pos);
        result = result || right;
    }
    return result;
}

bool ConstraintEvaluator::parseAndExpression(const std::vector<Token>& tokens, size_t& pos) {
    bool result = parseNotExpression(tokens, pos);
    while (pos < tokens.size() && tokens[pos].type == TokenType::AND) {
        ++pos;
        bool right = parseNotExpression(tokens, pos);
        result = result && right;
    }
    return result;
}

bool ConstraintEvaluator::parseNotExpression(const std::vector<Token>& tokens, size_t& pos) {
    if (pos < tokens.size() && tokens[pos].type == TokenType::NOT) {
        ++pos;
        return !parseNotExpression(tokens, pos);
    }
    return parseComparison(tokens, pos);
}

bool ConstraintEvaluator::parseComparison(const std::vector<Token>& tokens, size_t& pos) {
    bool result = parsePrimary(tokens, pos);
    if (pos < tokens.size() && tokens[pos].type == TokenType::OPERATOR) {
    }
    return result;
}

bool ConstraintEvaluator::parsePrimary(const std::vector<Token>& tokens, size_t& pos) {
    if (pos >= tokens.size()) {
        throw std::runtime_error("Unexpected end of expression");
    }
    const Token& token = tokens[pos];
    if (token.type == TokenType::LPAREN) {
        ++pos;
        bool result = parseExpression(tokens, pos);
        if (pos >= tokens.size() || tokens[pos].type != TokenType::RPAREN) {
            throw std::runtime_error("Missing closing parenthesis");
        }
        ++pos;
        return result;
    }
    
    if (token.type == TokenType::NUMBER) {
        ++pos;
        return token.value != "0" && token.value != "0.0";
    }
    if (token.type == TokenType::IDENTIFIER) {
        std::string var_name = token.value;
        ++pos;
        if (pos < tokens.size() && tokens[pos].type == TokenType::OPERATOR) {
            std::string op = tokens[pos].value;
            ++pos;
            if (pos >= tokens.size()) {
                throw std::runtime_error("Expected value after operator");
            }
            VariableValue right_val;
            if (tokens[pos].type == TokenType::NUMBER) {
                try {
                    if (tokens[pos].value.find('.') != std::string::npos) {
                        right_val = VariableValue(std::stod(tokens[pos].value));
                    } else {
                        right_val = VariableValue(std::stoi(tokens[pos].value));
                    }
                } catch (...) {
                    right_val = VariableValue(tokens[pos].value);
                }
            } else if (tokens[pos].type == TokenType::STRING) {
                right_val = VariableValue(tokens[pos].value);
            } else if (tokens[pos].type == TokenType::IDENTIFIER) {
                if (var_mgr_.exists(tokens[pos].value)) {
                    right_val = var_mgr_.get(tokens[pos].value);
                } else {
                    throw std::runtime_error("Variable not found: " + tokens[pos].value);
                }
            }
            ++pos;
            if (!var_mgr_.exists(var_name)) {
                throw std::runtime_error("Variable not found: " + var_name);
            }
            VariableValue left_val = var_mgr_.get(var_name);
            return compare(left_val, op, right_val);
        }
        if (var_mgr_.exists(var_name)) {
            return var_mgr_.get(var_name).asBool();
        } else {
            throw std::runtime_error("Variable not found: " + var_name);
        }
    }
    throw std::runtime_error("Unexpected token");
}

bool ConstraintEvaluator::compare(const VariableValue& left, const std::string& op, const VariableValue& right) {
    if (left.getType() == VariableValue::Type::STRING || right.getType() == VariableValue::Type::STRING) {
        std::string left_str = left.asString();
        std::string right_str = right.asString();
        if (op == "==" || op == "=") return left_str == right_str;
        if (op == "!=") return left_str != right_str;
        if (op == "<") return left_str < right_str;
        if (op == ">") return left_str > right_str;
        if (op == "<=") return left_str <= right_str;
        if (op == ">=") return left_str >= right_str;
    }
    try {
        double left_num = left.asDouble();
        double right_num = right.asDouble();
        const double epsilon = 1e-9;
        if (op == "==" || op == "=") return std::abs(left_num - right_num) < epsilon;
        if (op == "!=") return std::abs(left_num - right_num) >= epsilon;
        if (op == "<") return left_num < right_num;
        if (op == ">") return left_num > right_num;
        if (op == "<=") return left_num <= right_num;
        if (op == ">=") return left_num >= right_num;
    } catch (...) {
    }
    return false;
}

void ConstraintEvaluator::log(const std::string& message) const {
    if (debug_enabled_) {
        evaluation_log_.push_back(message);
    }
}

std::string ConstraintEvaluator::trim(const std::string& s) const {
    size_t start = 0;
    while (start < s.length() && std::isspace(s[start])) ++start;
    size_t end = s.length();
    while (end > start && std::isspace(s[end - 1])) --end;
    return s.substr(start, end - start);
}

std::time_t ConstraintEvaluator::parseTimeString(const std::string& time_str) const {
    struct std::tm tm = {};
    std::istringstream ss(time_str);
    ss >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S");
    return std::mktime(&tm);
}

bool ConstraintEvaluator::compareTime(const std::string& time1, const std::string& op, const std::string& time2) const {
    try {
        std::time_t t1 = parseTimeString(time1);
        std::time_t t2 = parseTimeString(time2);
        if (op == "==" || op == "=") return t1 == t2;
        if (op == "!=") return t1 != t2;
        if (op == "<") return t1 < t2;
        if (op == ">") return t1 > t2;
        if (op == "<=") return t1 <= t2;
        if (op == ">=") return t1 >= t2;
    } catch (...) {
        return false;
    }
    return false;
}