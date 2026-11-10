<?xml version="1.0" encoding="utf-8"?>
<!DOCTYPE TS>
<TS version="2.1" language="zh_CN">
<context>
    <name>GamebryoSaveGameInfoWidget</name>
    <message>
        <source>Date</source>
        <translation>时间</translation>
    </message>
    <message>
        <source>None</source>
        <translation>无</translation>
    </message>
    <message>
        <source>Level</source>
        <translation>等级：</translation>
    </message>
    <message>
        <source>Missing ESPs</source>
        <translation>缺失的 esp</translation>
    </message>
    <message>
        <source>Missing ESLs</source>
        <translation>缺失的 esl</translation>
    </message>
    <message>
        <source>Location</source>
        <translation>地点：</translation>
    </message>
    <message>
        <source>Save #</source>
        <translation>存档 #</translation>
    </message>
    <message>
        <source>Character</source>
        <translation>角色：</translation>
    </message>
    <message>
        <source>Has Script Extender Data</source>
        <translation>有脚本扩展器数据</translation>
    </message>
</context>
<context>
    <name>StarfieldModDataContent</name>
    <message>
        <source>Video</source>
        <translation>视频</translation>
    </message>
    <message>
        <source>Geometries</source>
        <translation>地形</translation>
    </message>
    <message>
        <source>Materials</source>
        <translation>材料</translation>
    </message>
</context>
<context>
    <name>GamebryoModDataContent</name>
    <message>
        <source>FaceGen Data</source>
        <translation>FaceGen（面部生成）数据</translation>
    </message>
    <message>
        <source>Plugins (ESP/ESM/ESL)</source>
        <translation>插件 (esp/esm/esl)</translation>
    </message>
    <message>
        <source>Sound or Music</source>
        <translation>音效或音乐</translation>
    </message>
    <message>
        <source>Script Extender Files</source>
        <translation>脚本扩展器文件</translation>
    </message>
    <message>
        <source>Meshes</source>
        <translation>模型</translation>
    </message>
    <message>
        <source>Bethesda Archive</source>
        <translation>贝塞斯达封装包</translation>
    </message>
    <message>
        <source>MCM Configuration</source>
        <translation>MCM 配置</translation>
    </message>
    <message>
        <source>Optional Plugins</source>
        <translation>可选插件</translation>
    </message>
    <message>
        <source>ModGroup Files</source>
        <translation>模组分组文件</translation>
    </message>
    <message>
        <source>SkyProc Patcher</source>
        <translation>SkyProc 补丁器</translation>
    </message>
    <message>
        <source>Interface</source>
        <translation>界面</translation>
    </message>
    <message>
        <source>INI Files</source>
        <translation>INI 文件</translation>
    </message>
    <message>
        <source>Textures</source>
        <translation>贴图</translation>
    </message>
    <message>
        <source>Scripts (Papyrus)</source>
        <translation>Papyrus 脚本</translation>
    </message>
    <message>
        <source>Script Extender Plugin</source>
        <translation>脚本扩展器插件</translation>
    </message>
</context>
<context>
    <name>QObject</name>
    <message>
        <source>ContentCatalog.txt appears to be corrupt: %1</source>
        <translation>ContentCatalog.txt 文件似乎已损坏：%1</translation>
    </message>
    <message>
        <source>failed to query registry path (read): %1</source>
        <translation>无法查询注册表路径（读取）： %1</translation>
    </message>
    <message>
        <source>Some of your plugins have invalid names! These plugins can not be loaded by the game. Please see mo_interface.log for a list of affected plugins and rename them.</source>
        <translation>您的某些插件名无效！这些插件不会被游戏载入。请查看 mo_interface.log 来确认哪些插件受影响并重命名它们。</translation>
    </message>
    <message>
        <source>%1, #%2, Level %3, %4</source>
        <translation>%1, #%2, 等级 %3, %4</translation>
    </message>
    <message>
        <source>wrong file format - expected %1 got %2</source>
        <translation>文件格式错误 - 预期为 %1，当前为 %2</translation>
    </message>
    <message>
        <source>failed to open %1</source>
        <translation>无法打开 %1</translation>
    </message>
    <message>
        <source>failed to query registry path (preflight): %1</source>
        <translation>无法查询注册表路径（预检）：%1</translation>
    </message>
</context>
<context>
    <name>GameStarfield</name>
    <message>
        <source>Starfield Support Plugin</source>
        <translation>《星空》支持插件</translation>
    </message>
    <message>
        <source>Show a warning when plugins.txt management is invalid.</source>
        <translation>显示 plugins.txt 管理无效讯息。</translation>
    </message>
    <message>
        <source>sTestFile entries are present</source>
        <translation>sTestFile 条目存在</translation>
    </message>
    <message>
        <source>&lt;p&gt;ESP plugins are not ideal for Starfield. In addition to being unable to sort them alongside ESM or master-flagged plugins, certain record references are always kept loaded by the game. This consumes unnecessary resources and limits the game&apos;s ability to load what it needs.&lt;/p&gt;&lt;p&gt;Ideally, plugins should be saved as ESM files upon release. It can also be released as an ESL plugin, however there are additional concerns with the way light plugins are currently handled and should only be used when absolutely certain about what you&apos;re doing.&lt;/p&gt;&lt;p&gt;Notably, xEdit does not currently support saving ESP files.&lt;/p&gt;&lt;h4&gt;Current ESPs:&lt;/h4&gt;&lt;p&gt;%1&lt;/p&gt;</source>
        <translation>&lt;p&gt;ESP 插件并不适合《星空》。它们无法与 ESM 或前置插件一起排序，其记录引用也会一直处于加载状态。这会导致非必需的资源消耗，限制游戏加载其所需内容的能力。&lt;/p&gt;&lt;p&gt;理想情况下，发布插件时应将其保存为 ESM 文件；也可发布为 ESL 插件，但当前处理 ESL 插件的方式上还存在其它问题，所以请在您知道自己在做什么的情况下执行此种操作。&lt;/p&gt;&lt;p&gt;并且 xEdit 当前并不支持保存 ESP 文件。&lt;/p&gt;&lt;h4&gt;当前 ESP：&lt;/h4&gt;&lt;p&gt;%1&lt;/p&gt;</translation>
    </message>
    <message>
        <source>Show a warning when ESP plugins are enabled in the load order.</source>
        <translation>显示加载顺序内有 ESP 插件开启的警告讯息。</translation>
    </message>
    <message>
        <source>You have active ESP plugins in Starfield</source>
        <translation>您有激活的《星空》esp 插件</translation>
    </message>
    <message>
        <source>Adds support for the game Starfield.</source>
        <translation>增加对《星空》的支持。</translation>
    </message>
    <message>
        <source>&lt;p&gt;You have plugin managment enabled but you still have sTestFile settings in your StarfieldCustom.ini. These must be removed or the game will not read the plugins.txt file. Management is still disabled.&lt;/p&gt;</source>
        <translation>&lt;p&gt;您已开启插件管理，但您的 StarfieldCustom.ini 内仍有 sTestFile 设置。您需将其移除，否则游戏不会读取 plugins.txt 插件。插件管理仍在禁用状态。&lt;/p&gt;</translation>
    </message>
    <message>
        <source>Utilize Starfield.ccc to affix core plugin load order (will override existing file).</source>
        <translation>使用 Starfield.ccc 来设置核心插件的加载顺序（将覆盖现有文件）。</translation>
    </message>
</context>
</TS>
