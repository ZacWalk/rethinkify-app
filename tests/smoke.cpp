#include <string_view>

int main()
{
    constexpr std::string_view name = "noterad";
    return name.empty() ? 1 : 0;
}
