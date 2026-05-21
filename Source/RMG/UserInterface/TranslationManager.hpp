#ifndef TRANSLATIONMANAGER_HPP
#define TRANSLATIONMANAGER_HPP

#include <QList>
#include <QTranslator>
#include <QString>
#include <QStringList>

class QCoreApplication;

namespace UserInterface
{
struct TranslationLanguage
{
    QString code;
    QString displayName;
};

class TranslationManager
{
  public:
    static TranslationManager& instance();

    QList<TranslationLanguage> availableLanguages() const;
    QString configuredLanguage() const;
    QString effectiveLanguage(const QString& languageCode) const;

    bool applyConfiguredLanguage(QCoreApplication* app);
    bool applyLanguage(QCoreApplication* app, const QString& languageCode);

  private:
    TranslationManager() = default;

    QString normalizedLanguage(const QString& languageCode) const;
    QStringList translationDirectories() const;
    bool loadTranslatorFromDirectories(QTranslator& translator, const QString& fileName) const;

    QTranslator m_appTranslator;
    QTranslator m_qtTranslator;
    QString m_currentLanguage;
};
} // namespace UserInterface

#endif // TRANSLATIONMANAGER_HPP
