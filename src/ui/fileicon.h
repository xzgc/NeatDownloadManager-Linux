#pragma once
#include <QIcon>
#include <QString>

namespace neat {

// Per-extension file icon for the download list. Resolution order:
// 1. a Papirus (GPL-3) mimetype PNG bundled at :/icons/ft/ — identical on
//    every machine, no dependence on distro icon themes;
// 2. the desktop theme's icon, only if the bundle is somehow missing;
// 3. generic per-category icon, then text-x-generic.
QIcon fileIconForName(const QString &fileName);

} // namespace neat
