"""Stage a self-contained project using an explicit local dylib, with no downloads."""
import shutil
from pathlib import Path


def stage(project, plugin, directory):
    project, plugin = Path(project), Path(plugin)
    if (project / '_geometry2d_original_main.lua').exists():
        raise ValueError('Reserved staging entry-point name already exists')
    if any(path.is_symlink() for path in project.rglob('*')):
        raise ValueError('Local-plugin staging requires a self-contained project without symlinks')
    target = Path(directory) / 'project'
    shutil.copytree(project, target, ignore=shutil.ignore_patterns('.git', '.DS_Store', '*.local.json'))
    settings = target / 'build.settings'
    original = settings.read_text() if settings.exists() else 'settings = {}\n'
    settings.write_text(original + '\n-- Local staging only; never edit the source settings.\nsettings.plugins = nil\n')
    shutil.copy2(plugin, target / 'plugin_geometry2d.dylib')
    (target / 'main.lua').rename(target / '_geometry2d_original_main.lua')
    (target / 'main.lua').write_text('''-- Generated local-only entry point; fail rather than load another plugin.
local binary = system.pathForFile("plugin_geometry2d.dylib", system.ResourceDirectory)
local loader, message = package.loadlib(binary, "luaopen_plugin_geometry2d")
assert(loader, message)
package.loaded["plugin.geometry2d"] = nil
package.preload["plugin.geometry2d"] = loader
local main = system.pathForFile("_geometry2d_original_main.lua", system.ResourceDirectory)
assert(loadfile(main))()
''')
    return target
