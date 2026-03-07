#include <iostream>
#include <vector>

int main() {
    std::vector<int> nums = {1, 2, 3, 4, 5};
    for (auto x : nums) {
        std::cout << x << " ";
    }
    std::cout << std::endl;
    return 0;
}