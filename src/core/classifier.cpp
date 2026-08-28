#include "core/classifier.hpp"
#include "core/strutil.hpp"

namespace lb {

Category ClassifyFile(const std::wstring& path, const RuleSet& rules) {
    Category c = rules.Match(LowerExt(path));
    return c == Category::None ? Category::Other : c;
}

}  // namespace lb
