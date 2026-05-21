#include "TranslationManager.hpp"

#include <RMG-Core/Directories.hpp>
#include <RMG-Core/Settings.hpp>

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QLibraryInfo>
#include <QLocale>

using namespace UserInterface;

TranslationManager& TranslationManager::instance()
{
    static TranslationManager manager;
    return manager;
}

QList<TranslationLanguage> TranslationManager::availableLanguages() const
{
    return {
        {"system", QCoreApplication::translate("TranslationManager", "System default")},
        {"en", QCoreApplication::translate("TranslationManager", "English")},
        {"ja", QCoreApplication::translate("TranslationManager", "Japanese")},
    };
}

QString TranslationManager::configuredLanguage() const
{
    return normalizedLanguage(QString::fromStdString(CoreSettingsGetStringValue(SettingsID::GUI_Language)));
}

QString TranslationManager::effectiveLanguage(const QString& languageCode) const
{
    const QString normalized = normalizedLanguage(languageCode);
    if (normalized == "system")
    {
        return QLocale::system().language() == QLocale::Japanese ? "ja" : "en";
    }

    return normalized;
}

bool TranslationManager::applyConfiguredLanguage(QCoreApplication* app)
{
    return applyLanguage(app, configuredLanguage());
}

bool TranslationManager::applyLanguage(QCoreApplication* app, const QString& languageCode)
{
    if (app == nullptr)
    {
        return false;
    }

    const QString language = effectiveLanguage(languageCode);
    app->removeTranslator(&m_appTranslator);
    app->removeTranslator(&m_qtTranslator);

    if (language == "en")
    {
        m_currentLanguage = language;
        return true;
    }

    const QString qtFileName = "qtbase_" + language + ".qm";
    bool qtLoaded = m_qtTranslator.load("qtbase_" + language, QLibraryInfo::path(QLibraryInfo::TranslationsPath));
    if (!qtLoaded)
    {
        qtLoaded = loadTranslatorFromDirectories(m_qtTranslator, qtFileName);
    }
    if (qtLoaded)
    {
        app->installTranslator(&m_qtTranslator);
    }

    const bool appLoaded = loadTranslatorFromDirectories(m_appTranslator, "rmgk_" + language + ".qm");
    if (appLoaded)
    {
        app->installTranslator(&m_appTranslator);
    }

    m_currentLanguage = language;
    return appLoaded;
}

QString TranslationManager::normalizedLanguage(const QString& languageCode) const
{
    const QString normalized = languageCode.trimmed().toLower().replace('-', '_');
    if (normalized == "ja" || normalized.startsWith("ja_"))
    {
        return "ja";
    }
    if (normalized == "en" || normalized.startsWith("en_"))
    {
        return "en";
    }

    return "system";
}

QStringList TranslationManager::translationDirectories() const
{
    QStringList directories;

    const QString sharedDataDirectory = QString::fromStdString(CoreGetSharedDataDirectory().string());
    if (!sharedDataDirectory.isEmpty())
    {
        directories.append(QDir(sharedDataDirectory).filePath("Translations"));
    }

    const QString applicationDirectory = QCoreApplication::applicationDirPath();
    if (!applicationDirectory.isEmpty())
    {
        directories.append(QDir(applicationDirectory).filePath("Data/Translations"));
        directories.append(QDir(applicationDirectory).filePath("Translations"));
    }

    directories.removeDuplicates();
    return directories;
}

bool TranslationManager::loadTranslatorFromDirectories(QTranslator& translator, const QString& fileName) const
{
    for (const QString& directory : translationDirectories())
    {
        const QString filePath = QDir(directory).filePath(fileName);
        if (QFileInfo::exists(filePath) && translator.load(filePath))
        {
            return true;
        }
    }

    return false;
}
