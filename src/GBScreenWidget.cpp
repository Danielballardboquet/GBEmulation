/**
 * @file GBScreenWidget.cpp
 * @brief Qt widget that displays the 160x144 LCD framebuffer of the emulator
 *        with nearest neighbour scaling and translates keyboard input into
 *        joypad presses.
 *
 * The widget keeps the 10:9 aspect ratio of the LCD and letterboxes the image
 * with integer scaling so pixels stay crisp. When no frame is available it
 * draws a placeholder message.
 *
 * @see https://doc.qt.io/qt-6/qwidget.html      Qt QWidget base class
 * @see https://doc.qt.io/qt-6/qpainter.html     Qt painting (drawImage)
 * @see https://gbdev.io/pandocs/LCDC_and_STAT.html   LCD dimensions used here
 * @see https://gbdev.io/pandocs/Joypad_Input.html    Joypad button layout
 */

#include "ui/GBScreenWidget.h"

#include "emulation/Emulation.h"
#include "emulation/Types.h"

#include <QFocusEvent>
#include <QKeyEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QResizeEvent>

#include <cstring>

namespace ui {

namespace {

/**
 * @brief Default text shown while no emulator frame is available.
 */
const QString kDefaultPlaceholder = QObject::tr("No ROM loaded.\nOpen a ROM with Ctrl+O.");

} // namespace

GBScreenWidget::GBScreenWidget(QWidget *parent)
    : QWidget(parent)
    , m_placeholder(kDefaultPlaceholder)
    , m_keyBindings(defaultKeyBindings())
    , m_actionBindings(defaultUiBindings())
{
    setFocusPolicy(Qt::StrongFocus);
    setMinimumSize(gb::kScreenWidth, gb::kScreenHeight);
    updateTargetRect();
}

GBScreenWidget::~GBScreenWidget() = default;

void GBScreenWidget::setEmulator(gb::Emulation *emulator)
{
    m_emulator = emulator;
    refreshFrame();
}

void GBScreenWidget::setKeyBindings(
    const std::array<QVector<int>, kJoypadButtonCount> &bindings)
{
    m_keyBindings = bindings;
    // Release every key that may no longer be mapped.
    for (int index = 0; index < 8; ++index) {
        if (m_held[index]) {
            m_held[index] = false;
            emit buttonChanged(index, false);
        }
    }
}

void GBScreenWidget::setActionBindings(
    const std::array<QVector<int>, kUiActionCount> &bindings)
{
    m_actionBindings = bindings;
}

/**
 * @see https://doc.qt.io/qt-6/qimage.html#QImage-4  QImage RGBA8888 format
 */
void GBScreenWidget::refreshFrame()
{
    if (!m_emulator) {
        m_image = QImage();
        update();
        return;
    }

    const gb::u8 *buffer = m_emulator->frameBuffer();
    if (m_image.isNull() || m_image.size() != QSize(gb::kScreenWidth, gb::kScreenHeight)
        || m_image.format() != QImage::Format_RGBA8888) {
        m_image = QImage(gb::kScreenWidth, gb::kScreenHeight, QImage::Format_RGBA8888);
    }

    // The RGBA8888 image of 160 pixels per row has no scanline padding, so the
    // whole framebuffer can be copied in one go.
    std::memcpy(m_image.bits(), buffer, static_cast<std::size_t>(gb::kScreenWidth * gb::kScreenHeight * 4));
    update();
}

void GBScreenWidget::setPlaceholderText(const QString &message)
{
    m_placeholder = message;
    update();
}

QSize GBScreenWidget::preferredSize(const QSize &availableSize)
{
    if (!availableSize.isValid() || availableSize.width() < 1 || availableSize.height() < 1) {
        return QSize(gb::kScreenWidth, gb::kScreenHeight);
    }

    const int scale = qMax(1, qMin(availableSize.width() / gb::kScreenWidth,
                                   availableSize.height() / gb::kScreenHeight));
    return QSize(gb::kScreenWidth * scale, gb::kScreenHeight * scale);
}

/**
 * @brief Paints the framebuffer or the placeholder text.
 * @param event The paint event.
 */
void GBScreenWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.fillRect(rect(), QColor(0x00, 0x00, 0x00));

    if (m_image.isNull()) {
        painter.setPen(QColor(0x9E, 0x9E, 0x9E));
        painter.drawText(rect(), Qt::AlignCenter, m_placeholder);
        return;
    }

    // FastTransformation (the default) performs nearest neighbour scaling, which
    // is what a retro LCD wants; SmoothPixmapTransform is explicitly disabled.
    painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
    painter.drawImage(m_targetRect, m_image);
}

void GBScreenWidget::resizeEvent(QResizeEvent *event)
{
    updateTargetRect();
    QWidget::resizeEvent(event);
}

void GBScreenWidget::keyPressEvent(QKeyEvent *event)
{
    if (event->isAutoRepeat()) {
        event->accept();
        return;
    }

    const UiAction action = actionForKey(event->key());
    if (action != static_cast<UiAction>(kUiActionCount)) {
        // Global actions (pause/fullscreen/exit) take priority over joypad.
        emit uiActionTriggered(action);
        event->accept();
        return;
    }

    const int index = buttonForKey(event->key());
    if (index >= 0 && !m_held[index]) {
        m_held[index] = true;
        emit buttonChanged(index, true);
    }
    event->accept();
}

void GBScreenWidget::keyReleaseEvent(QKeyEvent *event)
{
    if (event->isAutoRepeat()) {
        event->accept();
        return;
    }

    // Global actions are triggered on press only; the release is ignored.
    const UiAction action = actionForKey(event->key());
    if (action != static_cast<UiAction>(kUiActionCount)) {
        event->accept();
        return;
    }

    const int index = buttonForKey(event->key());
    if (index >= 0 && m_held[index]) {
        m_held[index] = false;
        emit buttonChanged(index, false);
    }
    event->accept();
}

void GBScreenWidget::focusOutEvent(QFocusEvent *event)
{
    for (int index = 0; index < 8; ++index) {
        if (m_held[index]) {
            m_held[index] = false;
            emit buttonChanged(index, false);
        }
    }
    QWidget::focusOutEvent(event);
}

/**
 * @brief Maps a Qt key code to a joypad button index using the configured
 *        keyboard layout.
 * @param key The Qt key code.
 * @return The button index (0..7) or -1 when the key is not mapped.
 * @see https://doc.qt.io/qt-6/qt.html#Key-enum Qt key code reference
 * @see https://gbdev.io/pandocs/Joypad_Input.html Joypad bit layout
 */
int GBScreenWidget::buttonForKey(int key) const
{
    for (int index = 0; index < kJoypadButtonCount; ++index) {
        if (m_keyBindings[index].contains(key)) {
            return index;
        }
    }
    return -1;
}

/**
 * @brief Maps a Qt key code to a global action.
 * @param key The Qt key code.
 * @return The triggered action or UiActionCount when not mapped.
 */
UiAction GBScreenWidget::actionForKey(int key) const
{
    for (int index = 0; index < kUiActionCount; ++index) {
        if (m_actionBindings[index].contains(key)) {
            return static_cast<UiAction>(index);
        }
    }
    return static_cast<UiAction>(kUiActionCount);
}

void GBScreenWidget::updateTargetRect()
{
    const int scale = qMax(1, qMin(width() / gb::kScreenWidth, height() / gb::kScreenHeight));
    const int targetWidth = gb::kScreenWidth * scale;
    const int targetHeight = gb::kScreenHeight * scale;
    m_targetRect = QRect((width() - targetWidth) / 2, (height() - targetHeight) / 2,
                         targetWidth, targetHeight);
}

} // namespace ui
