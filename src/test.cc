#include <vector>

struct alignas(64) X { int val; };
int main() {
    std::vector<X> v;
    v.emplace_back(X{1});
    return 0;
}
   
