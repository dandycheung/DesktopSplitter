#include "ui/Theme.h"

namespace ds {
namespace theme {

HFONT CreateUiFont(int dpi, double dipSize, int weight, const wchar_t* face) {
    LOGFONTW lf{};
    lf.lfHeight = -DipRound(dipSize, dpi);
    lf.lfWeight = weight;
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfQuality = CLEARTYPE_QUALITY;
    wcsncpy_s(lf.lfFaceName, face, _TRUNCATE);
    return CreateFontIndirectW(&lf);
}

HFONT CreateMonoFont(int dpi, double dipSize, int weight) {
    return CreateUiFont(dpi, dipSize, weight, L"Consolas");
}

} // namespace theme
} // namespace ds
