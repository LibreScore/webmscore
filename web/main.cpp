
#include <emscripten/emscripten.h>

#include "libmscore/excerpt.h"
#include "libmscore/exports.h"
#include "libmscore/mscore.h"
#include "libmscore/score.h"
#include "libmscore/text.h"

/**
 * helper functions
 */

/**
 * pack length-prefixed data
 */
QByteArray packData(QByteArray data, qint64 size) {
    QByteArray sizeData = QByteArray((const char*)&size, 4);

    QBuffer result;
    result.open(QIODevice::ReadWrite);
    result.write(QByteArray(8, '\0'));  // padding
    result.write(sizeData);
    result.write(data);
    result.close();

    return result.data();
}

/**
 * It's so weird that the first 8 bytes of data would be overwritten by some random things
 * @todo PLEASE HELP - I'm not familiar with emscripten
 */
QByteArray padData(QByteArray data) {
    return QByteArray(8, '\0').append(data);
}

Ms::Score* maybeUseExcerpt(Ms::Score* score, int excerptId) {
    // -1 means the full score
    if (excerptId >= 0) {
        QList<Ms::Excerpt*> excerpts = score->excerpts();

        if (excerptId >= excerpts.size()) {
            throw(QString("Not a valid excerptId.")); 
        }

        qDebug("useExcerpt: %d", excerptId);
        score = excerpts[excerptId]->partScore();
    }

    return score;
}

/**
 * the MSCZ/MSCX file format version
 */
int _version() {
    return Ms::MSCVERSION;
}

/**
 * init libmscore
 */
void _init(int argc, char** argv) {
    new QGuiApplication(argc, argv);

    Ms::MScore::noGui = true;
    Ms::MScore::debugMode = true;
    Ms::MScore::init();
}

/**
 * load the score data (a MSCZ/MSCX file buffer)
 */
uintptr_t _load(const char* name, const char* data, const uint32_t size) {
    QString _name = QString::fromUtf8(name);

    QBuffer buffer;
    buffer.setData(data, size);
    buffer.open(QIODevice::ReadOnly);

    Ms::MasterScore* score = new Ms::MasterScore(Ms::MScore::baseStyle());
    score->setMovements(new Ms::Movements());
    score->setName(_name);

    score->loadMsc(_name, &buffer, true);

    // mscore/file.cpp#L2257 readScore
    score->rebuildMidiMapping();
    score->setSoloMute();
    for (auto s : score->scoreList()) {
        s->setPlaylistDirty();
        s->addLayoutFlags(Ms::LayoutFlag::FIX_PITCH_VELO);
        s->setLayoutAll();
    }
    score->updateChannel();

    // do layout ...
    score->update();

    return reinterpret_cast<uintptr_t>(score);
}

/**
 * get the score title
 */
QByteArray _title(uintptr_t score_ptr) {
    Ms::MasterScore* score = reinterpret_cast<Ms::MasterScore*>(score_ptr);

    // code from MuseScore::saveMetadataJSON
    QString title;
    Ms::Text* t = score->getText(Ms::Tid::TITLE);
    if (t)
        title = t->plainText();
    if (title.isEmpty())
        title = score->metaTag("workTitle");
    if (title.isEmpty())
        title = score->title();

    return padData(
        title.toUtf8()
    );
}

/**
 * get the number of pages
 */
int _npages(uintptr_t score_ptr, int excerptId) {
    auto score = reinterpret_cast<Ms::Score*>(score_ptr);
    score = maybeUseExcerpt(score, excerptId);
    return score->npages();
}

/**
 * export score as MusicXML file
 */
const char* _saveXml(uintptr_t score_ptr, int excerptId) {
    auto score = reinterpret_cast<Ms::Score*>(score_ptr);
    score = maybeUseExcerpt(score, excerptId);

    QBuffer buffer;
    buffer.open(QIODevice::WriteOnly);

    Ms::saveXml(score, &buffer);
    qDebug("saveXml: excerpt %d, size %lld bytes", excerptId, buffer.size());

    // MusicXML is plain text
    return padData(
        QString(buffer.data()).toUtf8()
    );
}

/**
 * export score as compressed MusicXML file
 */
const char* _saveMxl(uintptr_t score_ptr, int excerptId) {
    auto score = reinterpret_cast<Ms::Score*>(score_ptr);
    score = maybeUseExcerpt(score, excerptId);

    QBuffer buffer;
    buffer.open(QIODevice::ReadWrite);

    // compressed MusicXML
    Ms::saveMxl(score, &buffer);

    auto size = buffer.size();
    qDebug("saveMxl: excerpt %d, size %lld", excerptId, size);

    return packData(buffer.data(), size);
}

/**
 * export score as SVG
 */
const char* _saveSvg(uintptr_t score_ptr, int pageNumber, bool drawPageBackground, int excerptId) {
    auto score = reinterpret_cast<Ms::Score*>(score_ptr);
    score = maybeUseExcerpt(score, excerptId);

    QBuffer buffer;
    buffer.open(QIODevice::WriteOnly);

    score->switchToPageMode(); // not really required, as the default _layoutMode is LayoutMode::PAGE
    Ms::saveSvg(score, &buffer, pageNumber, drawPageBackground);
    qDebug("saveSvg: excerpt %d, page index %d, size %lld bytes", excerptId, pageNumber, buffer.size());

    // SVG is plain text
    return padData(
        QString(buffer.data()).toUtf8()
    );
}

/**
 * export score as PNG
 */
const char* _savePng(uintptr_t score_ptr, int pageNumber, bool drawPageBackground, bool transparent, int excerptId) {
    auto score = reinterpret_cast<Ms::Score*>(score_ptr);
    score = maybeUseExcerpt(score, excerptId);

    QBuffer buffer;
    buffer.open(QIODevice::WriteOnly);

    score->switchToPageMode();
    Ms::savePng(score, &buffer, pageNumber, drawPageBackground, transparent);

    auto size = buffer.size();
    qDebug("savePng: excerpt %d, page index %d, drawPageBackground %d, transparent %d, size %lld bytes", excerptId, pageNumber, drawPageBackground, transparent, size);

    return packData(buffer.data(), size);
}

/**
 * export score as PDF
 */
const char* _savePdf(uintptr_t score_ptr, int excerptId) {
    auto score = reinterpret_cast<Ms::Score*>(score_ptr);
    score = maybeUseExcerpt(score, excerptId);

    QBuffer buffer;
    buffer.open(QIODevice::ReadWrite);

    Ms::savePdf(score, &buffer);

    auto size = buffer.size();
    qDebug("savePdf: excerpt %d, size %lld", excerptId, size);

    return packData(buffer.data(), size);
}

/**
 * export score as MIDI
 */
const char* _saveMidi(uintptr_t score_ptr, bool midiExpandRepeats, bool exportRPNs, int excerptId) {
    auto score = reinterpret_cast<Ms::Score*>(score_ptr);
    score = maybeUseExcerpt(score, excerptId);

    QBuffer buffer;
    buffer.open(QIODevice::ReadWrite);

    Ms::saveMidi(score, &buffer, midiExpandRepeats, exportRPNs);

    auto size = buffer.size();
    qDebug("saveMidi: excerpt %d, midiExpandRepeats %d, exportRPNs %d, size %lld", excerptId, midiExpandRepeats, exportRPNs, size);

    return packData(buffer.data(), size);
}

/**
 * save positions of measures or segments (if the `ofSegments` param == true) as JSON
 */
const char* _savePositions(uintptr_t score_ptr, bool ofSegments, int excerptId) {
    auto score = reinterpret_cast<Ms::Score*>(score_ptr);
    score = maybeUseExcerpt(score, excerptId);

    score->switchToPageMode();
    QJsonObject json = Ms::savePositions(score, ofSegments);
    QJsonDocument saveDoc(json);

    auto data = saveDoc.toJson(QJsonDocument::Compact);  // UTF-8 encoded JSON data
    qDebug("savePositions: excerpt %d, ofSegments %d, file size %d", excerptId, ofSegments, data.size());

    // JSON is plain text
    return padData(data);
}

/**
 * save score metadata as JSON
 */
const char* _saveMetadata(uintptr_t score_ptr) {
    Ms::MasterScore* score = reinterpret_cast<Ms::MasterScore*>(score_ptr);

    QJsonObject json = saveMetadataJSON(score);
    QJsonDocument saveDoc(json);

    // JSON is plain text
    return padData(
        saveDoc.toJson()  // UTF-8 encoded JSON data
    );
}

/**
 * export functions (can only be C functions)
 */
extern "C" {

    EMSCRIPTEN_KEEPALIVE
    int version() {
        return _version();
    };

    EMSCRIPTEN_KEEPALIVE
    void init(int argc, char** argv) {
        return _init(argc, argv);
    };

    EMSCRIPTEN_KEEPALIVE
    uintptr_t load(const char* name, const char* data, const uint32_t size) {
        return _load(name, data, size);
    };

    EMSCRIPTEN_KEEPALIVE
    const char* title(uintptr_t score_ptr) {
        return _title(score_ptr);
    };

    EMSCRIPTEN_KEEPALIVE
    int npages(uintptr_t score_ptr, int excerptId) {
        return _npages(score_ptr, excerptId);
    };

    EMSCRIPTEN_KEEPALIVE
    const char* saveXml(uintptr_t score_ptr, int excerptId = -1) {
        return _saveXml(score_ptr, excerptId);
    };

    EMSCRIPTEN_KEEPALIVE
    const char* saveMxl(uintptr_t score_ptr, int excerptId = -1) {
        return _saveMxl(score_ptr, excerptId);
    };

    EMSCRIPTEN_KEEPALIVE
    const char* saveSvg(uintptr_t score_ptr, int pageNumber, bool drawPageBackground, int excerptId = -1) {
        return _saveSvg(score_ptr, pageNumber, drawPageBackground, excerptId);
    };

    EMSCRIPTEN_KEEPALIVE
    const char* savePng(uintptr_t score_ptr, int pageNumber, bool drawPageBackground, bool transparent, int excerptId = -1) {
        return _savePng(score_ptr, pageNumber, drawPageBackground, transparent, excerptId);
    };

    EMSCRIPTEN_KEEPALIVE
    const char* savePdf(uintptr_t score_ptr, int excerptId = -1) {
        return _savePdf(score_ptr, excerptId);
    };

    EMSCRIPTEN_KEEPALIVE
    const char* saveMidi(uintptr_t score_ptr, bool midiExpandRepeats, bool exportRPNs, int excerptId = -1) {
        return _saveMidi(score_ptr, midiExpandRepeats, exportRPNs, excerptId);
    };

    EMSCRIPTEN_KEEPALIVE
    const char* savePositions(uintptr_t score_ptr, bool ofSegments, int excerptId = -1) {
        return _savePositions(score_ptr, ofSegments, excerptId);
    };

    EMSCRIPTEN_KEEPALIVE
    const char* saveMetadata(uintptr_t score_ptr) {
        return _saveMetadata(score_ptr);
    };

}
