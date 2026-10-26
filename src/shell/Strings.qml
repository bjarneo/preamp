import QtQuick
import Quickshell
import Quickshell.Io

// The widget's strings in the language picked under Setup > Language. Omarchy
// before 4.5 has no Commons.Translations, so the widget reads the language
// file and its own catalogs in i18n/ itself.
//
// A string missing from the language falls back to its base language (pt-BR
// to pt), then to en.json, then to the key itself. A language switch
// re-renders the bindings that call t() without a restart.
QtObject {
  id: root

  readonly property string directory: String(Qt.resolvedUrl("i18n")).replace(/^file:\/\//, "")
  readonly property string configHome: Quickshell.env("XDG_CONFIG_HOME") || (Quickshell.env("HOME") + "/.config")
  readonly property string language: {
    var code = String(languageSetting.text() || "").trim()
    return /^[a-z]{2,3}(-[A-Za-z]{2})?$/.test(code) ? code : "en"
  }
  readonly property string baseLanguage: language.split("-")[0]

  readonly property var strings: {
    var merged = ({})
    var layers = [parse(englishCatalog.text()), parse(baseCatalog.text()), parse(languageCatalog.text())]
    for (var i = 0; i < layers.length; i++)
      for (var key in layers[i])
        if (typeof layers[i][key] === "string" && layers[i][key].length > 0) merged[key] = layers[i][key]
    return merged
  }

  function t(key, fallback) {
    var value = root.strings[key]
    if (typeof value === "string") return value
    return fallback !== undefined ? fallback : key
  }

  function parse(raw) {
    try {
      var parsed = JSON.parse(raw || "{}")
      return (parsed && typeof parsed === "object" && !Array.isArray(parsed)) ? parsed : ({})
    } catch (e) {
      return ({})
    }
  }

  // The same file the shell reads. It is missing until a language is picked.
  property FileView languageSetting: FileView {
    path: Quickshell.env("OMARCHY_LANGUAGE_FILE") || (root.configHome + "/omarchy/language")
    watchChanges: true
    printErrors: false
    blockLoading: true
    onFileChanged: reload()
  }

  property FileView englishCatalog: FileView {
    path: root.directory + "/en.json"
    watchChanges: true
    printErrors: false
    blockLoading: true
    onFileChanged: reload()
  }

  // Only read for a regional code. For "da" the language catalog covers it.
  property FileView baseCatalog: FileView {
    path: root.baseLanguage !== root.language && root.baseLanguage !== "en" ? root.directory + "/" + root.baseLanguage + ".json" : ""
    watchChanges: true
    printErrors: false
    blockLoading: true
    onFileChanged: reload()
  }

  property FileView languageCatalog: FileView {
    path: root.language !== "en" ? root.directory + "/" + root.language + ".json" : ""
    watchChanges: true
    printErrors: false
    blockLoading: true
    onFileChanged: reload()
  }
}
