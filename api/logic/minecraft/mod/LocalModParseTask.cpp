#include "LocalModParseTask.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonValue>
#include <quazip.h>
#include <quazipfile.h>

#include "settings/INIFile.h"
#include "FileSystem.h"

namespace {

// NEW format
// https://github.com/MinecraftForge/FML/wiki/FML-mod-information-file/6f62b37cea040daf350dc253eae6326dd9c822c3

// OLD format:
// https://github.com/MinecraftForge/FML/wiki/FML-mod-information-file/5bf6a2d05145ec79387acc0d45c958642fb049fc
std::shared_ptr<ModDetails> ReadMCModInfo(QByteArray contents)
{
    auto getInfoFromArray = [&](QJsonArray arr)->std::shared_ptr<ModDetails>
    {
        if (!arr.at(0).isObject()) {
            return nullptr;
        }
        std::shared_ptr<ModDetails> details = std::make_shared<ModDetails>();
        auto firstObj = arr.at(0).toObject();
        details->mod_id = firstObj.value("modid").toString();
        auto name = firstObj.value("name").toString();
        // NOTE: ignore stupid example mods copies where the author didn't even bother to change the name
        if(name != "Example Mod") {
            details->name = name;
        }
        details->version = firstObj.value("version").toString();
        details->updateurl = firstObj.value("updateUrl").toString();
        auto homeurl = firstObj.value("url").toString().trimmed();
        if(!homeurl.isEmpty())
        {
            // fix up url.
            if (!homeurl.startsWith("http://") && !homeurl.startsWith("https://") && !homeurl.startsWith("ftp://"))
            {
                homeurl.prepend("http://");
            }
        }
        details->homeurl = homeurl;
        details->description = firstObj.value("description").toString();
        QJsonArray authors = firstObj.value("authorList").toArray();
        if (authors.size() == 0) {
            // FIXME: what is the format of this? is there any?
            authors = firstObj.value("authors").toArray();
        }

        for (auto author: authors)
        {
            details->authors.append(author.toString());
        }
        details->credits = firstObj.value("credits").toString();
        return details;
    };
    QJsonParseError jsonError;
    QJsonDocument jsonDoc = QJsonDocument::fromJson(contents, &jsonError);
    // this is the very old format that had just the array
    if (jsonDoc.isArray())
    {
        return getInfoFromArray(jsonDoc.array());
    }
    else if (jsonDoc.isObject())
    {
        auto val = jsonDoc.object().value("modinfoversion");
        if(val.isUndefined()) {
            val = jsonDoc.object().value("modListVersion");
        }
        int version = val.toDouble();
        if (version != 2)
        {
            qCritical() << "BAD stuff happened to mod json:";
            qCritical() << contents;
            return nullptr;
        }
        auto arrVal = jsonDoc.object().value("modlist");
        if(arrVal.isUndefined()) {
            arrVal = jsonDoc.object().value("modList");
        }
        if (arrVal.isArray())
        {
            return getInfoFromArray(arrVal.toArray());
        }
    }
    return nullptr;
}

// https://fabricmc.net/wiki/documentation:fabric_mod_json
std::shared_ptr<ModDetails> ReadFabricModInfo(QByteArray contents)
{
    QJsonParseError jsonError;
    QJsonDocument jsonDoc = QJsonDocument::fromJson(contents, &jsonError);
    auto object = jsonDoc.object();
    auto schemaVersion = object.contains("schemaVersion") ? object.value("schemaVersion").toInt(0) : 0;

    std::shared_ptr<ModDetails> details = std::make_shared<ModDetails>();

    details->mod_id = object.value("id").toString();
    details->version = object.value("version").toString();

    details->name = object.contains("name") ? object.value("name").toString() : details->mod_id;
    details->description = object.value("description").toString();

    if (schemaVersion >= 1)
    {
        QJsonArray authors = object.value("authors").toArray();
        for (auto author: authors)
        {
            if(author.isObject()) {
                details->authors.append(author.toObject().value("name").toString());
            }
            else {
                details->authors.append(author.toString());
            }
        }

        if (object.contains("contact"))
        {
            QJsonObject contact = object.value("contact").toObject();

            if (contact.contains("homepage"))
            {
                details->homeurl = contact.value("homepage").toString();
            }
        }
    }
    return details;
}

std::shared_ptr<ModDetails> ReadForgeInfo(QByteArray contents)
{
    std::shared_ptr<ModDetails> details = std::make_shared<ModDetails>();
    // Read the data
    details->name = "Minecraft Forge";
    details->mod_id = "Forge";
    details->homeurl = "http://www.minecraftforge.net/forum/";
    INIFile ini;
    if (!ini.loadFile(contents))
        return details;

    QString major = ini.get("forge.major.number", "0").toString();
    QString minor = ini.get("forge.minor.number", "0").toString();
    QString revision = ini.get("forge.revision.number", "0").toString();
    QString build = ini.get("forge.build.number", "0").toString();

    details->version = major + "." + minor + "." + revision + "." + build;
    return details;
}

std::shared_ptr<ModDetails> ReadLiteModInfo(QByteArray contents)
{
    std::shared_ptr<ModDetails> details = std::make_shared<ModDetails>();
    QJsonParseError jsonError;
    QJsonDocument jsonDoc = QJsonDocument::fromJson(contents, &jsonError);
    auto object = jsonDoc.object();
    if (object.contains("name"))
    {
        details->mod_id = details->name = object.value("name").toString();
    }
    if (object.contains("version"))
    {
        details->version = object.value("version").toString("");
    }
    else
    {
        details->version = object.value("revision").toString("");
    }
    details->mcversion = object.value("mcversion").toString();
    auto author = object.value("author").toString();
    if(!author.isEmpty()) {
        details->authors.append(author);
    }
    details->description = object.value("description").toString();
    details->homeurl = object.value("url").toString();
    return details;
}

}

LocalModParseTask::LocalModParseTask(int token, Mod::ModType type, const QFileInfo& modFile):
    m_token(token),
    m_type(type),
    m_modFile(modFile),
    m_result(new Result())
{
}

void LocalModParseTask::processAsZip()
{
    QuaZip zip(m_modFile.filePath());
    if (!zip.open(QuaZip::mdUnzip))
        return;

    QuaZipFile file(&zip);

    if (zip.setCurrentFile("mcmod.info"))
    {
        if (!file.open(QIODevice::ReadOnly))
        {
            zip.close();
            return;
        }

        m_result->details = ReadMCModInfo(file.readAll());
        file.close();
        zip.close();
        return;
    }
    else if (zip.setCurrentFile("fabric.mod.json"))
    {
        if (!file.open(QIODevice::ReadOnly))
        {
            zip.close();
            return;
        }

        m_result->details = ReadFabricModInfo(file.readAll());
        file.close();
        zip.close();
        return;
    }
    else if (zip.setCurrentFile("forgeversion.properties"))
    {
        if (!file.open(QIODevice::ReadOnly))
        {
            zip.close();
            return;
        }

        m_result->details = ReadForgeInfo(file.readAll());
        file.close();
        zip.close();
        return;
    }

    zip.close();
}

void LocalModParseTask::processAsFolder()
{
    QFileInfo mcmod_info(FS::PathCombine(m_modFile.filePath(), "mcmod.info"));
    if (mcmod_info.isFile())
    {
        QFile mcmod(mcmod_info.filePath());
        if (!mcmod.open(QIODevice::ReadOnly))
            return;
        auto data = mcmod.readAll();
        if (data.isEmpty() || data.isNull())
            return;
        m_result->details = ReadMCModInfo(data);
    }
}

void LocalModParseTask::processAsLitemod()
{
    QuaZip zip(m_modFile.filePath());
    if (!zip.open(QuaZip::mdUnzip))
        return;

    QuaZipFile file(&zip);

    if (zip.setCurrentFile("litemod.json"))
    {
        if (!file.open(QIODevice::ReadOnly))
        {
            zip.close();
            return;
        }

        m_result->details = ReadLiteModInfo(file.readAll());
        file.close();
    }
    zip.close();
}

void LocalModParseTask::run()
{
    switch(m_type)
    {
        case Mod::MOD_ZIPFILE:
            processAsZip();
            break;
        case Mod::MOD_FOLDER:
            processAsFolder();
            break;
        case Mod::MOD_LITEMOD:
            processAsLitemod();
            break;
        default:
            break;
    }
    emit finished(m_token);
}
