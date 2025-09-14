#pragma once

#include <unordered_map>
#include <string>
#include <vector>

#include "test_harness.h"

template <typename ExprT>
class ScopeGuard
{
public:
    ScopeGuard(ExprT expr)
        : callback(std::move(expr)) {
    }

    ~ScopeGuard() { if (!disarmed) callback(); }

    void disarm() { disarmed = true; }

private:
    ExprT callback;
    bool disarmed{ false };
};

std::unordered_map<std::string, std::vector<test_case*>>& test_groups();
