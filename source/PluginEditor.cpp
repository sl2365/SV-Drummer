#include "PluginEditor.h"

#include <BinaryData.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <utility>
#include <vector>

namespace
{
const juce::Colour backgroundColour (0xff111316);
const juce::Colour panelColour (0xff191c20);
const juce::Colour raisedPanelColour (0xff20242a);
const juce::Colour lineColour (0xff343940);
const juce::Colour textColour (0xffe8ebef);
const juce::Colour mutedTextColour (0xff8d949e);
const juce::Colour trimMarkerColour (0xffef5a5a);
const juce::Colour loopMarkerColour (0xff23838a);
constexpr int baseEditorWidth = 1280;
constexpr int baseEditorHeight = 800;

juce::Colour getPadColour (int index)
{
    static const std::array<juce::Colour, 16> colours
    {
        juce::Colour (0xffdf5b57), juce::Colour (0xffd77c75),
        juce::Colour (0xffdf874b), juce::Colour (0xffc5a24f),
        juce::Colour (0xffcf6389), juce::Colour (0xffad5b8c),
        juce::Colour (0xff8348a7), juce::Colour (0xff7163b6),
        juce::Colour (0xff526fc2), juce::Colour (0xff438dcc),
        juce::Colour (0xff3ca7b6), juce::Colour (0xff45a58b),
        juce::Colour (0xff59a965), juce::Colour (0xff84ad50),
        juce::Colour (0xffb5a943), juce::Colour (0xffcc7850)
    };

    return colours[static_cast<std::size_t> (juce::jlimit (0, 15, index))];
}

juce::String midiNoteDescription (int note)
{
    return juce::String (note) + "  "
         + juce::MidiMessage::getMidiNoteName (note, true, true, 4);
}

bool isSupportedPath (const juce::String& path)
{
    const juce::File file (path);
    return SVDrummerAudioProcessor::isSupportedAudioFile (file)
        || SVDrummerAudioProcessor::isSupportedKitFile (file)
        || SVDrummerAudioProcessor::isSupportedProjectFile (file);
}

}

class SVDrummerLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    SVDrummerLookAndFeel()
    {
        setColour (juce::Label::textColourId, textColour);
        setColour (juce::Label::textWhenEditingColourId, textColour);
        setColour (juce::Slider::textBoxTextColourId, textColour);
        setColour (juce::Slider::textBoxBackgroundColourId, panelColour);
        setColour (juce::Slider::textBoxOutlineColourId, lineColour);
        setColour (juce::PopupMenu::backgroundColourId, raisedPanelColour);
        setColour (juce::PopupMenu::textColourId, textColour);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, juce::Colour (0xff405064));
        setColour (juce::ScrollBar::backgroundColourId, panelColour);
        setColour (juce::ScrollBar::trackColourId, raisedPanelColour.darker (0.28f));
        setColour (juce::ScrollBar::thumbColourId, juce::Colour (0xff3c9fc1));
    }

    juce::Label* createSliderTextBox (juce::Slider& slider) override
    {
        auto* label = juce::LookAndFeel_V4::createSliderTextBox (slider);
        label->setFont (juce::FontOptions (9.5f, juce::Font::bold));
        return label;
    }

    void drawButtonBackground (juce::Graphics& g,
                               juce::Button& button,
                               const juce::Colour& background,
                               bool highlighted,
                               bool down) override
    {
        auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
        auto colour = background;

        if (down)
            colour = colour.brighter (0.18f);
        else if (highlighted)
            colour = colour.brighter (0.09f);

        const bool connected = button.isConnectedOnLeft()
                            || button.isConnectedOnRight()
                            || button.isConnectedOnTop()
                            || button.isConnectedOnBottom();

        if (connected)
        {
            const bool curveTopLeft = ! button.isConnectedOnLeft()
                                       && ! button.isConnectedOnTop();
            const bool curveTopRight = ! button.isConnectedOnRight()
                                        && ! button.isConnectedOnTop();
            juce::Path shape;
            shape.addRoundedRectangle (
                bounds.getX(), bounds.getY(), bounds.getWidth(), bounds.getHeight(),
                3.0f, 3.0f,
                curveTopLeft,
                curveTopRight,
                ! button.isConnectedOnLeft() && ! button.isConnectedOnBottom(),
                ! button.isConnectedOnRight() && ! button.isConnectedOnBottom());
            g.setColour (colour);
            g.fillPath (shape);
            g.setColour (lineColour.brighter (highlighted ? 0.22f : 0.0f));

            if (button.getToggleState() && button.isConnectedOnBottom())
            {
                const float radius = 3.0f;
                juce::Path outline;
                outline.startNewSubPath (bounds.getX(), bounds.getBottom());

                if (curveTopLeft)
                {
                    outline.lineTo (bounds.getX(), bounds.getY() + radius);
                    outline.quadraticTo (bounds.getX(), bounds.getY(),
                                         bounds.getX() + radius, bounds.getY());
                }
                else
                {
                    outline.lineTo (bounds.getX(), bounds.getY());
                }

                if (curveTopRight)
                {
                    outline.lineTo (bounds.getRight() - radius, bounds.getY());
                    outline.quadraticTo (bounds.getRight(), bounds.getY(),
                                         bounds.getRight(), bounds.getY() + radius);
                }
                else
                {
                    outline.lineTo (bounds.getRight(), bounds.getY());
                }

                outline.lineTo (bounds.getRight(), bounds.getBottom());
                g.strokePath (outline, juce::PathStrokeType (1.0f));
            }
            else
            {
                g.strokePath (shape, juce::PathStrokeType (1.0f));
            }

            return;
        }

        g.setColour (colour);
        g.fillRoundedRectangle (bounds, 3.0f);
        g.setColour (lineColour.brighter (highlighted ? 0.22f : 0.0f));
        g.drawRoundedRectangle (bounds, 3.0f, 1.0f);
    }

    void drawRotarySlider (juce::Graphics& g,
                           int x, int y, int width, int height,
                           float position,
                           float startAngle,
                           float endAngle,
                           juce::Slider& slider) override
    {
        const auto radius = static_cast<float> (juce::jmin (width, height)) * 0.40f;
        const auto centre = juce::Point<float> (static_cast<float> (x + width) * 0.5f,
                                                static_cast<float> (y + height) * 0.5f);
        const auto angle = startAngle + position * (endAngle - startAngle);
        const auto accent = slider.findColour (juce::Slider::rotarySliderFillColourId);

        juce::Path track;
        track.addCentredArc (centre.x, centre.y, radius, radius,
                             0.0f, startAngle, endAngle, true);
        g.setColour (lineColour);
        g.strokePath (track, juce::PathStrokeType (4.0f,
                                                   juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));

        juce::Path fill;
        fill.addCentredArc (centre.x, centre.y, radius, radius,
                            0.0f, startAngle, angle, true);
        g.setColour (accent);
        g.strokePath (fill, juce::PathStrokeType (4.0f,
                                                  juce::PathStrokeType::curved,
                                                  juce::PathStrokeType::rounded));

        g.setColour (juce::Colour (0xff2b3036));
        g.fillEllipse (centre.x - radius * 0.72f,
                       centre.y - radius * 0.72f,
                       radius * 1.44f,
                       radius * 1.44f);
        g.setColour (accent);
        g.drawLine (centre.x, centre.y,
                    centre.x + std::sin (angle) * radius * 0.58f,
                    centre.y - std::cos (angle) * radius * 0.58f,
                    2.0f);
    }
};

class SVDrummerIconButton final : public juce::Button
{
public:
    enum class Icon
    {
        add,
        remove,
        refresh,
        preview,
        save,
        openFolder,
        samples,
        kits,
        patterns,
        projects
    };

    SVDrummerIconButton (const juce::String& accessibleName, Icon iconType)
        : juce::Button (accessibleName), icon (iconType)
    {
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    void setAttentionState (bool shouldShowAttention)
    {
        if (attention != shouldShowAttention)
        {
            attention = shouldShowAttention;
            repaint();
        }
    }

    void paintButton (juce::Graphics& g,
                      bool highlighted,
                      bool down) override
    {
        auto bounds = getLocalBounds().toFloat().reduced (0.5f);
        auto background = (attention || getToggleState())
                            ? juce::Colour (0xff3e536a)
                            : raisedPanelColour;

        if (down)
            background = background.brighter (0.18f);
        else if (highlighted)
            background = background.brighter (0.09f);

        g.setColour (background);
        g.fillRoundedRectangle (bounds, 3.0f);
        g.setColour (lineColour.brighter (highlighted ? 0.22f : 0.0f));
        g.drawRoundedRectangle (bounds, 3.0f, 1.0f);

        const bool browserTabIcon = icon == Icon::samples
                                 || icon == Icon::kits
                                 || icon == Icon::patterns
                                 || icon == Icon::projects;
        auto iconBounds = browserTabIcon
                            ? juce::Rectangle<float> (22.0f, 16.0f)
                                  .withCentre (bounds.getCentre())
                            : bounds.reduced (7.0f, 5.0f);
        const auto centre = iconBounds.getCentre();
        g.setColour (textColour.withAlpha (isEnabled() ? 0.94f : 0.34f));

        if (icon == Icon::add)
        {
            g.drawLine (iconBounds.getX(), centre.y,
                        iconBounds.getRight(), centre.y, 1.8f);
            g.drawLine (centre.x, iconBounds.getY(),
                        centre.x, iconBounds.getBottom(), 1.8f);
        }
        else if (icon == Icon::remove)
        {
            g.drawLine (iconBounds.getX(), iconBounds.getY(),
                        iconBounds.getRight(), iconBounds.getBottom(), 1.8f);
            g.drawLine (iconBounds.getRight(), iconBounds.getY(),
                        iconBounds.getX(), iconBounds.getBottom(), 1.8f);
        }
        else if (icon == Icon::refresh)
        {
            const float radius = juce::jmin (iconBounds.getWidth(),
                                             iconBounds.getHeight()) * 0.39f;
            constexpr float topStart = -juce::MathConstants<float>::pi * 0.75f;
            constexpr float topEnd = juce::MathConstants<float>::pi * 0.25f;
            constexpr float bottomStart = juce::MathConstants<float>::pi * 0.25f;
            constexpr float bottomEnd = juce::MathConstants<float>::pi * 1.25f;
            juce::Path arrows;
            arrows.addCentredArc (centre.x, centre.y, radius, radius, 0.0f,
                                  topStart, topEnd, true);
            arrows.addCentredArc (centre.x, centre.y, radius, radius, 0.0f,
                                  bottomStart, bottomEnd, true);
            g.strokePath (arrows, juce::PathStrokeType (
                1.8f, juce::PathStrokeType::curved,
                juce::PathStrokeType::rounded));

            const auto drawArrowHead = [&] (float angle)
            {
                const juce::Point<float> tip (
                    centre.x + std::sin (angle) * radius,
                    centre.y - std::cos (angle) * radius);
                const juce::Point<float> tangent (
                    std::cos (angle), std::sin (angle));
                const juce::Point<float> normal (-tangent.y, tangent.x);
                const auto base = tip - tangent * (radius * 0.52f);
                juce::Path head;
                head.startNewSubPath (base + normal * (radius * 0.25f));
                head.lineTo (tip);
                head.lineTo (base - normal * (radius * 0.25f));
                g.strokePath (head, juce::PathStrokeType (
                    1.8f, juce::PathStrokeType::curved,
                    juce::PathStrokeType::rounded));
            };

            drawArrowHead (topEnd);
            drawArrowHead (bottomEnd);
        }
        else if (icon == Icon::preview)
        {
            juce::Path triangle;
            triangle.addTriangle (iconBounds.getX() + 2.0f,
                                  iconBounds.getY(),
                                  iconBounds.getX() + 2.0f,
                                  iconBounds.getBottom(),
                                  iconBounds.getRight(), centre.y);
            g.fillPath (triangle);
        }
        else if (icon == Icon::save)
        {
            auto disk = iconBounds.reduced (1.0f, 0.0f);
            g.drawRoundedRectangle (disk, 1.5f, 1.6f);
            auto label = disk.removeFromTop (disk.getHeight() * 0.44f)
                             .reduced (3.0f, 1.0f);
            g.fillRect (label);
            auto hub = disk.reduced (3.0f, 2.0f);
            g.drawRect (hub, 1.2f);
        }
        else if (icon == Icon::openFolder)
        {
            auto folder = iconBounds.reduced (1.0f, 2.0f);
            juce::Path shape;
            shape.startNewSubPath (folder.getX(), folder.getY() + 3.0f);
            shape.lineTo (folder.getX() + folder.getWidth() * 0.38f,
                          folder.getY() + 3.0f);
            shape.lineTo (folder.getX() + folder.getWidth() * 0.49f,
                          folder.getY() + 0.5f);
            shape.lineTo (folder.getRight(), folder.getY() + 0.5f);
            shape.lineTo (folder.getRight(), folder.getBottom());
            shape.lineTo (folder.getX(), folder.getBottom());
            shape.closeSubPath();
            g.strokePath (shape, juce::PathStrokeType (
                1.6f, juce::PathStrokeType::curved,
                juce::PathStrokeType::rounded));
            g.drawLine (folder.getX() + 2.0f, folder.getY() + 6.0f,
                        folder.getRight() - 2.0f, folder.getY() + 6.0f, 1.3f);
        }
        else if (icon == Icon::samples)
        {
            juce::Path wave;
            wave.startNewSubPath (iconBounds.getX(), centre.y);

            for (int point = 1; point <= 6; ++point)
            {
                const float fraction = static_cast<float> (point) / 6.0f;
                const float x = iconBounds.getX() + iconBounds.getWidth() * fraction;
                const float y = centre.y
                              + (point % 2 == 0 ? -1.0f : 1.0f)
                                    * iconBounds.getHeight() * 0.30f;
                wave.lineTo (x, y);
            }

            g.strokePath (wave, juce::PathStrokeType (
                1.7f, juce::PathStrokeType::curved,
                juce::PathStrokeType::rounded));
        }
        else if (icon == Icon::kits)
        {
            const float cell = juce::jmin (iconBounds.getWidth(),
                                           iconBounds.getHeight()) * 0.34f;
            const float gap = 2.0f;
            const auto grid = juce::Rectangle<float> (
                cell * 2.0f + gap, cell * 2.0f + gap).withCentre (centre);

            for (int row = 0; row < 2; ++row)
                for (int column = 0; column < 2; ++column)
                    g.drawRoundedRectangle (
                        grid.getX() + static_cast<float> (column) * (cell + gap),
                        grid.getY() + static_cast<float> (row) * (cell + gap),
                        cell, cell, 1.0f, 1.3f);
        }
        else if (icon == Icon::patterns)
        {
            const float stepWidth = iconBounds.getWidth() / 7.0f;

            for (int step = 0; step < 4; ++step)
            {
                const float height = iconBounds.getHeight()
                                   * (0.38f + 0.16f * static_cast<float> (step % 3));
                const float x = iconBounds.getX()
                              + static_cast<float> (step * 2) * stepWidth;
                g.fillRoundedRectangle (x, iconBounds.getBottom() - height,
                                        stepWidth, height, 0.8f);
            }
        }
        else
        {
            auto document = iconBounds.reduced (2.0f, 0.5f);
            juce::Path page;
            const float fold = 4.0f;
            page.startNewSubPath (document.getX(), document.getY());
            page.lineTo (document.getRight() - fold, document.getY());
            page.lineTo (document.getRight(), document.getY() + fold);
            page.lineTo (document.getRight(), document.getBottom());
            page.lineTo (document.getX(), document.getBottom());
            page.closeSubPath();
            g.strokePath (page, juce::PathStrokeType (1.5f));
            g.drawLine (document.getRight() - fold, document.getY(),
                        document.getRight() - fold, document.getY() + fold, 1.2f);
            g.drawLine (document.getRight() - fold, document.getY() + fold,
                        document.getRight(), document.getY() + fold, 1.2f);
            g.drawHorizontalLine (juce::roundToInt (centre.y + 2.0f),
                                  document.getX() + 3.0f,
                                  document.getRight() - 3.0f);
        }
    }

private:
    Icon icon;
    bool attention = false;
};

class SVDrummerEnvelopeSlider final : public juce::Slider
{
public:
    void setWheelStep (double newStep) noexcept
    {
        wheelStep = juce::jmax (0.0, newStep);
    }

    void mouseWheelMove (const juce::MouseEvent& event,
                         const juce::MouseWheelDetails& wheel) override
    {
        if (! isEnabled() || wheelStep <= 0.0)
        {
            juce::Slider::mouseWheelMove (event, wheel);
            return;
        }

        double movement = std::abs (wheel.deltaX) > std::abs (wheel.deltaY)
                            ? -static_cast<double> (wheel.deltaX)
                            : static_cast<double> (wheel.deltaY);

        if (wheel.isReversed)
            movement = -movement;

        if (movement == 0.0)
            return;

        setValue (getValue() + (movement > 0.0 ? wheelStep : -wheelStep),
                  juce::sendNotificationSync);
    }

private:
    double wheelStep = 1.0;
};

class SampleTreeRow;

class SampleTreeItem final : public juce::TreeViewItem
{
public:
    SampleTreeItem (juce::File itemFile,
                    juce::File libraryRoot,
                    bool topLevel,
                    SVDrummerAudioProcessor::BrowserMode browserMode)
        : file (std::move (itemFile)),
          rootFolder (std::move (libraryRoot)),
          isTopLevel (topLevel),
          mode (browserMode)
    {
    }

    bool mightContainSubItems() override
    {
        return file.isDirectory();
    }

    int getItemHeight() const override
    {
        return 24;
    }

    juce::String getUniqueName() const override
    {
        return file.getFullPathName();
    }

    void itemOpennessChanged (bool isNowOpen) override
    {
        if (! isNowOpen || ! file.isDirectory())
            return;

        clearSubItems();

        juce::Array<juce::File> directories;
        juce::Array<juce::File> samples;
        for (const auto& entry : juce::RangedDirectoryIterator (
                 file, false, "*", juce::File::findFilesAndDirectories))
        {
            const auto child = entry.getFile();

            if (entry.isHidden())
                continue;

            if (child.isDirectory())
                directories.add (child);
            else if (isSupportedFile (child))
                samples.add (child);
        }

        struct FileSorter
        {
            static int compareElements (const juce::File& first, const juce::File& second)
            {
                return first.getFileName().compareNatural (second.getFileName());
            }
        } sorter;

        directories.sort (sorter);
        samples.sort (sorter);

        for (const auto& directory : directories)
            addSubItem (new SampleTreeItem (
                directory, rootFolder, false, mode));

        for (const auto& sample : samples)
            addSubItem (new SampleTreeItem (
                sample, rootFolder, false, mode));
    }

    void paintItem (juce::Graphics& g, int width, int height) override
    {
        paintRow (g, width, height);
    }

    std::unique_ptr<juce::Component> createItemComponent() override;

    void paintRow (juce::Graphics& g, int width, int height)
    {
        const bool directory = file.isDirectory();
        const auto colour = directory ? (isTopLevel ? juce::Colour (0xffd9dde4)
                                                     : juce::Colour (0xffb6bdc8))
                                      : juce::Colour (0xffa4abb4);

        if (isSelected())
        {
            g.setColour (juce::Colour (0xff354252));
            g.fillRoundedRectangle (0.0f, 1.0f,
                                    static_cast<float> (width),
                                    static_cast<float> (height - 2), 2.0f);
        }

        const auto iconBounds = juce::Rectangle<float> (
            3.0f, (static_cast<float> (height) - 14.0f) * 0.5f, 14.0f, 14.0f);
        g.setColour (directory ? juce::Colour (0xffd09c50) : juce::Colour (0xff6c7887));

        if (directory)
        {
            g.fillRoundedRectangle (iconBounds, 1.5f);
            g.fillRect (juce::Rectangle<float> (
                5.0f, iconBounds.getY() - 2.0f, 7.0f, 4.0f));
        }
        else
        {
            const float middle = iconBounds.getCentreY();
            juce::Path wave;
            wave.startNewSubPath (iconBounds.getX(), middle);

            for (int point = 0; point < 7; ++point)
            {
                const float px = iconBounds.getX() + static_cast<float> (point) * 2.0f;
                const float py = middle + ((point % 2 == 0) ? -3.0f : 3.0f);
                wave.lineTo (px, py);
            }

            g.strokePath (wave, juce::PathStrokeType (1.4f));
        }

        g.setColour (colour);
        g.setFont (juce::FontOptions (directory ? 12.5f : 12.0f,
                                     directory ? juce::Font::bold
                                               : juce::Font::plain));
        g.drawText (file.getFileName(), 23, 0, width - 25, height,
                    juce::Justification::centredLeft, true);
    }

    const juce::File& getFile() const noexcept       { return file; }
    const juce::File& getRootFolder() const noexcept { return rootFolder; }
    bool isSupportedDragFile() const                 { return isSupportedFile (file); }

private:
    bool isSupportedFile (const juce::File& candidate) const
    {
        switch (mode)
        {
            case SVDrummerAudioProcessor::BrowserMode::kits:
                return SVDrummerAudioProcessor::isSupportedKitFile (candidate);
            case SVDrummerAudioProcessor::BrowserMode::patterns:
                return SVDrummerAudioProcessor::isSupportedPatternFile (candidate)
                    || SVDrummerAudioProcessor::isSupportedPatternSetFile (candidate);
            case SVDrummerAudioProcessor::BrowserMode::projects:
                return SVDrummerAudioProcessor::isSupportedProjectFile (candidate);
            case SVDrummerAudioProcessor::BrowserMode::samples:
            default:
                return SVDrummerAudioProcessor::isSupportedAudioFile (candidate);
        }
    }

    juce::File file;
    juce::File rootFolder;
    bool isTopLevel = false;
    SVDrummerAudioProcessor::BrowserMode mode =
        SVDrummerAudioProcessor::BrowserMode::samples;
};

class SampleTreeRow final : public juce::Component
{
public:
    explicit SampleTreeRow (SampleTreeItem& owner) : item (owner) {}

    void paint (juce::Graphics& g) override
    {
        item.paintRow (g, getWidth(), getHeight());
    }

    void mouseDown (const juce::MouseEvent&) override
    {
        dragStarted = false;
        item.setSelected (true, true);
    }

    void mouseDoubleClick (const juce::MouseEvent&) override
    {
        if (item.getFile().isDirectory())
            item.setOpen (! item.isOpen());
    }

    void mouseDrag (const juce::MouseEvent& event) override
    {
        if (dragStarted || event.getDistanceFromDragStart() < 5
            || ! item.getFile().existsAsFile()
            || ! item.isSupportedDragFile())
            return;

        if (auto* container = findParentComponentOfClass<juce::DragAndDropContainer>())
        {
            dragStarted = true;
            container->startDragging (item.getFile().getFullPathName(), this);
        }
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        dragStarted = false;
    }

private:
    SampleTreeItem& item;
    bool dragStarted = false;
};

std::unique_ptr<juce::Component> SampleTreeItem::createItemComponent()
{
    return std::make_unique<SampleTreeRow> (*this);
}

class SampleTreeRoot final : public juce::TreeViewItem
{
public:
    bool mightContainSubItems() override { return true; }
    juce::String getUniqueName() const override { return "SVDRUMMER_BROWSER_ROOT"; }
    void paintItem (juce::Graphics&, int, int) override {}
};

class SampleBrowserTree final : public juce::TreeView
{
public:
    SampleBrowserTree()
    {
        setRootItem (&rootItem);
        setRootItemVisible (false);
        setDefaultOpenness (false);
        setIndentSize (16);
        setColour (juce::TreeView::backgroundColourId, panelColour);
        setColour (juce::TreeView::linesColourId, lineColour);
    }

    ~SampleBrowserTree() override
    {
        setRootItem (nullptr);
    }

    void rebuild (const juce::StringArray& folders,
                  SVDrummerAudioProcessor::BrowserMode mode)
    {
        rootItem.clearSubItems();

        for (const auto& path : folders)
        {
            const juce::File folder (path);

            if (folder.isDirectory())
                rootItem.addSubItem (new SampleTreeItem (
                    folder, folder, true, mode));
        }

        rootItem.setOpen (true);

        repaint();
    }

    juce::File getSelectedRootFolder() const
    {
        if (auto* item = dynamic_cast<SampleTreeItem*> (getSelectedItem (0)))
            return item->getRootFolder();

        return {};
    }

    juce::File getSelectedPreviewFile() const
    {
        if (auto* item = dynamic_cast<SampleTreeItem*> (getSelectedItem (0)))
        {
            const auto selected = item->getFile();

            if (selected.existsAsFile()
                && SVDrummerAudioProcessor::isSupportedAudioFile (selected))
                return selected;
        }

        return {};
    }

    juce::File getSelectedFolder() const
    {
        if (auto* item = dynamic_cast<SampleTreeItem*> (getSelectedItem (0)))
        {
            const auto selected = item->getFile();

            if (selected.isDirectory())
                return selected;

            if (selected.existsAsFile())
                return selected.getParentDirectory();
        }

        return {};
    }

    bool hasItems() const noexcept
    {
        return rootItem.getNumSubItems() > 0;
    }

private:
    SampleTreeRoot rootItem;
};

class SVDrummerBrowserPanel final : public juce::Component
{
public:
    SVDrummerBrowserPanel (SVDrummerAudioProcessor& owner,
                           std::function<juce::Result()> saveCallback,
                           std::function<bool()> unsavedStateCallback)
        : processor (owner),
          onSaveRequested (std::move (saveCallback)),
          hasUnsavedChanges (std::move (unsavedStateCallback))
    {
        samplesButton.setClickingTogglesState (false);
        kitsButton.setClickingTogglesState (false);
        patternsButton.setClickingTogglesState (false);
        projectsButton.setClickingTogglesState (false);

        samplesButton.onClick = [this]
        {
            showBrowserMode (SVDrummerAudioProcessor::BrowserMode::samples);
        };
        kitsButton.onClick = [this]
        {
            showBrowserMode (SVDrummerAudioProcessor::BrowserMode::kits);
        };
        patternsButton.onClick = [this]
        {
            showBrowserMode (SVDrummerAudioProcessor::BrowserMode::patterns);
        };
        projectsButton.onClick = [this]
        {
            showBrowserMode (SVDrummerAudioProcessor::BrowserMode::projects);
        };
        addFolderButton.onClick = [this] { chooseFolder(); };
        removeFolderButton.onClick = [this]
        {
            const auto root = tree.getSelectedRootFolder();

            if (! root.isDirectory())
                return;

            juce::Component::SafePointer<SVDrummerBrowserPanel> safeThis (this);
            juce::AlertWindow::showOkCancelBox (
                juce::MessageBoxIconType::QuestionIcon,
                "Remove Sample Folder",
                "Remove this folder from the SV-Drummer browser?\n\n"
                    + root.getFullPathName()
                    + "\n\nThe folder and its files will not be deleted.",
                "Remove", "Cancel", this,
                juce::ModalCallbackFunction::create (
                    [safeThis, root] (int result)
                    {
                        if (safeThis == nullptr || result == 0)
                            return;

                        const auto saveResult =
                            safeThis->processor.removeBrowserFolder (root);

                        if (saveResult.failed())
                            juce::AlertWindow::showMessageBoxAsync (
                                juce::MessageBoxIconType::WarningIcon,
                                "SV-Drummer Browser Not Saved",
                                saveResult.getErrorMessage());

                        safeThis->refresh();
                        safeThis->refreshSaveState();
                    }));
        };
        refreshButton.onClick = [this] { refresh(); };
        previewButton.onClick = [this]
        {
            const auto selected = tree.getSelectedPreviewFile();

            if (! selected.existsAsFile())
            {
                juce::AlertWindow::showMessageBoxAsync (
                    juce::MessageBoxIconType::InfoIcon,
                    "SV-Drummer",
                    "Select an audio sample in the browser first.");
                return;
            }

            const auto result = processor.previewSampleFile (selected);

            if (result.failed())
                juce::AlertWindow::showMessageBoxAsync (
                    juce::MessageBoxIconType::WarningIcon,
                    "SV-Drummer",
                    "Could not preview the sample:\n\n" + result.getErrorMessage());
        };
        saveButton.onClick = [this]
        {
            if (mode == SVDrummerAudioProcessor::BrowserMode::kits)
                chooseKitSaveLocation();
            else if (mode == SVDrummerAudioProcessor::BrowserMode::patterns)
                choosePatternSaveLocation();
            else if (mode == SVDrummerAudioProcessor::BrowserMode::projects)
                chooseProjectSaveLocation();
        };
        openFolderButton.onClick = [this] { openCurrentBrowserFolder(); };
        samplesButton.setTooltip ("Samples");
        kitsButton.setTooltip ("Kits");
        patternsButton.setTooltip ("Patterns and Pattern Sets");
        projectsButton.setTooltip ("Projects");
        addFolderButton.setTooltip ("Add a sample-library folder");
        removeFolderButton.setTooltip ("Remove the selected folder from the browser");
        refreshButton.setTooltip ("Refresh the browser");
        previewButton.setTooltip ("Play the selected sample without loading it onto a pad");
        saveButton.setTooltip (
            "Save all pads, patterns, sequencer data, browser folders and GUI zoom");
        openFolderButton.setTooltip ("Open this library folder in Explorer");

        addAndMakeVisible (samplesButton);
        addAndMakeVisible (kitsButton);
        addAndMakeVisible (patternsButton);
        addAndMakeVisible (projectsButton);
        addAndMakeVisible (addFolderButton);
        addAndMakeVisible (removeFolderButton);
        addAndMakeVisible (refreshButton);
        addAndMakeVisible (previewButton);
        addAndMakeVisible (saveButton);
        addAndMakeVisible (openFolderButton);
        addAndMakeVisible (tree);
        addAndMakeVisible (emptyLabel);

        emptyLabel.setText ("Add one or more sample folders.\n\n"
                            "Drag supported samples from this tree onto a pad.",
                            juce::dontSendNotification);
        emptyLabel.setJustificationType (juce::Justification::centred);
        emptyLabel.setColour (juce::Label::textColourId, mutedTextColour);

        showBrowserMode (processor.getEditorBrowserMode());
        refreshSaveState();
    }

    ~SVDrummerBrowserPanel() override
    {
        saveUiState();
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (panelColour);
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 5.0f);
        g.setColour (lineColour);
        g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 5.0f, 1.0f);

        auto title = getLocalBounds().reduced (10).removeFromTop (24);
        g.setColour (textColour);
        g.setFont (14.0f);
        const auto titleText =
            mode == SVDrummerAudioProcessor::BrowserMode::samples ? "SAMPLE LIBRARIES"
          : mode == SVDrummerAudioProcessor::BrowserMode::kits ? "KIT LIBRARY"
          : mode == SVDrummerAudioProcessor::BrowserMode::patterns ? "PATTERN LIBRARY"
                                                                   : "PROJECT LIBRARY";
        g.drawText (titleText, title, juce::Justification::centredLeft);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (8);
        auto titleRow = area.removeFromTop (24);
        refreshButton.setBounds (titleRow.removeFromRight (28).reduced (1));
        area.removeFromTop (4);

        auto modeRow = area.removeFromTop (27);
        const int tabWidth = modeRow.getWidth() / 4;
        samplesButton.setBounds (modeRow.removeFromLeft (tabWidth).reduced (1));
        kitsButton.setBounds (modeRow.removeFromLeft (tabWidth).reduced (1));
        patternsButton.setBounds (modeRow.removeFromLeft (tabWidth).reduced (1));
        projectsButton.setBounds (modeRow.reduced (1));

        area.removeFromTop (6);
        auto tools = area.removeFromTop (26);

        const auto layoutToolButtons = [] (
            juce::Rectangle<int> row,
            std::initializer_list<juce::Button*> buttons)
        {
            constexpr int buttonWidth = 34;
            constexpr int gap = 9;
            const int count = static_cast<int> (buttons.size());
            const int totalWidth = count * buttonWidth + (count - 1) * gap;
            row = row.withSizeKeepingCentre (totalWidth, row.getHeight());

            for (auto* button : buttons)
            {
                button->setBounds (row.removeFromLeft (buttonWidth));
                row.removeFromLeft (gap);
            }
        };

        if (mode == SVDrummerAudioProcessor::BrowserMode::samples)
        {
            layoutToolButtons (tools, { &addFolderButton, &removeFolderButton,
                                        &previewButton, &openFolderButton });
        }
        else
        {
            layoutToolButtons (tools, { &saveButton, &openFolderButton });
        }
        area.removeFromTop (6);

        tree.setBounds (area);
        emptyLabel.setBounds (area.reduced (16));
    }

    void refresh (bool preserveCurrentTreeState = true)
    {
        std::unique_ptr<juce::XmlElement> treeState;

        if (preserveCurrentTreeState && tree.hasItems())
            treeState = tree.getOpennessState (true);
        else
            treeState = juce::XmlDocument::parse (
                processor.getEditorBrowserTreeState (mode));

        juce::StringArray folders;

        if (mode == SVDrummerAudioProcessor::BrowserMode::samples)
        {
            folders = processor.getBrowserFolders();
            tree.rebuild (folders, mode);
            emptyLabel.setVisible (folders.isEmpty());
        }
        else if (mode == SVDrummerAudioProcessor::BrowserMode::kits)
        {
            const auto kitDirectory = processor.getPortableKitsDirectory();
            kitDirectory.createDirectory();
            folders.add (kitDirectory.getFullPathName());
            tree.rebuild (folders, mode);
            emptyLabel.setVisible (kitDirectory.findChildFiles (
                juce::File::findFiles, true, "*.svkit").isEmpty());
        }
        else if (mode == SVDrummerAudioProcessor::BrowserMode::patterns)
        {
            const auto patternDirectory = processor.getPortablePatternsDirectory();
            patternDirectory.createDirectory();
            const auto patternsDirectory = patternDirectory.getChildFile ("Patterns");
            const auto patternSetsDirectory = patternDirectory.getChildFile ("Pattern Sets");
            patternsDirectory.createDirectory();
            patternSetsDirectory.createDirectory();
            folders.add (patternsDirectory.getFullPathName());
            folders.add (patternSetsDirectory.getFullPathName());
            tree.rebuild (folders, mode);
            const bool hasPatternFiles = ! patternsDirectory.findChildFiles (
                juce::File::findFiles, true, "*.svpattern").isEmpty()
                || ! patternSetsDirectory.findChildFiles (
                    juce::File::findFiles, true, "*.svpatternset").isEmpty();
            emptyLabel.setVisible (! hasPatternFiles);
        }
        else
        {
            const auto projectsDirectory = processor.getPortableProjectsDirectory();
            projectsDirectory.createDirectory();
            folders.add (projectsDirectory.getFullPathName());
            tree.rebuild (folders, mode);
            emptyLabel.setVisible (projectsDirectory.findChildFiles (
                juce::File::findFiles, true, "*.svproject").isEmpty());
        }

        if (treeState != nullptr)
            tree.restoreOpennessState (*treeState, true);
    }

    void saveUiState()
    {
        processor.setEditorBrowserMode (mode);

        if (tree.hasItems())
            if (const auto state = tree.getOpennessState (true))
                processor.setEditorBrowserTreeState (
                    mode, state->toString());
    }

    void refreshSaveState()
    {
        saveButton.setAttentionState (false);
    }

private:
    void showBrowserMode (SVDrummerAudioProcessor::BrowserMode newMode)
    {
        if (initialised)
            saveUiState();

        mode = newMode;
        processor.setEditorBrowserMode (mode);
        samplesButton.setToggleState (
            mode == SVDrummerAudioProcessor::BrowserMode::samples,
            juce::dontSendNotification);
        kitsButton.setToggleState (
            mode == SVDrummerAudioProcessor::BrowserMode::kits,
            juce::dontSendNotification);
        patternsButton.setToggleState (
            mode == SVDrummerAudioProcessor::BrowserMode::patterns,
            juce::dontSendNotification);
        projectsButton.setToggleState (
            mode == SVDrummerAudioProcessor::BrowserMode::projects,
            juce::dontSendNotification);

        tree.setVisible (true);
        const bool showingSamples =
            mode == SVDrummerAudioProcessor::BrowserMode::samples;
        addFolderButton.setVisible (showingSamples);
        removeFolderButton.setVisible (showingSamples);
        refreshButton.setVisible (true);
        previewButton.setVisible (showingSamples);
        saveButton.setVisible (! showingSamples);
        openFolderButton.setVisible (true);
        const auto saveTooltip =
            mode == SVDrummerAudioProcessor::BrowserMode::kits
                ? juce::String ("Save the current sixteen-pad setup as a Kit")
          : mode == SVDrummerAudioProcessor::BrowserMode::patterns
                ? juce::String ("Save the currently selected Pattern")
                : juce::String ("Save the complete Kit and Pattern Set as a Project");
        saveButton.setTooltip (saveTooltip);
        refreshSaveState();

        if (mode == SVDrummerAudioProcessor::BrowserMode::kits)
        {
            emptyLabel.setText ("Saved kits appear here.\n\n"
                                "Use Save to create a kit, then drag it onto any pad to load it.",
                                juce::dontSendNotification);
        }
        else if (mode == SVDrummerAudioProcessor::BrowserMode::patterns)
        {
            emptyLabel.setText ("Saved Patterns and Pattern Sets appear here.\n\n"
                                "Drag either file type onto any pattern slot.",
                                juce::dontSendNotification);
        }
        else if (mode == SVDrummerAudioProcessor::BrowserMode::projects)
        {
            emptyLabel.setText ("Saved Projects appear here.\n\n"
                                "Drag a Project onto any pad or Pattern button to load it.",
                                juce::dontSendNotification);
        }
        else
        {
            emptyLabel.setText ("Add one or more sample folders.\n\n"
                                "Drag supported samples from this tree onto a pad.",
                                juce::dontSendNotification);
        }

        resized();
        refresh (false);
        repaint();
        initialised = true;
    }

    void openCurrentBrowserFolder()
    {
        juce::File folder;

        if (mode == SVDrummerAudioProcessor::BrowserMode::samples)
        {
            folder = tree.getSelectedFolder();

            if (! folder.isDirectory())
                folder = processor.getPortableSamplesDirectory();
        }
        else if (mode == SVDrummerAudioProcessor::BrowserMode::kits)
        {
            folder = processor.getPortableKitsDirectory();
        }
        else if (mode == SVDrummerAudioProcessor::BrowserMode::patterns)
        {
            folder = processor.getPortablePatternsDirectory();
        }
        else
        {
            folder = processor.getPortableProjectsDirectory();
        }

        if (! folder.isDirectory() && folder.createDirectory().failed())
        {
            juce::AlertWindow::showMessageBoxAsync (
                juce::MessageBoxIconType::WarningIcon,
                "SV-Drummer",
                "The library folder could not be created:\n\n"
                    + folder.getFullPathName());
            return;
        }

        if (! folder.startAsProcess())
            juce::AlertWindow::showMessageBoxAsync (
                juce::MessageBoxIconType::WarningIcon,
                "SV-Drummer",
                "The folder could not be opened:\n\n"
                    + folder.getFullPathName());
    }

    void chooseFolder()
    {
        auto initialFolder = processor.getPortableSamplesDirectory();
        initialFolder.createDirectory();

        folderChooser = std::make_unique<juce::FileChooser> (
            "Add a sample-library folder", initialFolder, juce::String(), true);

        folderChooser->launchAsync (
            juce::FileBrowserComponent::openMode
                | juce::FileBrowserComponent::canSelectDirectories,
            [this] (const juce::FileChooser& chooser)
            {
                const auto result = chooser.getResult();

                if (result.isDirectory())
                {
                    const auto saveResult = processor.addBrowserFolder (result);

                    if (saveResult.failed())
                        juce::AlertWindow::showMessageBoxAsync (
                            juce::MessageBoxIconType::WarningIcon,
                            "SV-Drummer Browser Not Saved",
                            saveResult.getErrorMessage());

                    refresh();
                    refreshSaveState();
                }
            });
    }

    void chooseKitSaveLocation()
    {
        auto kitDirectory = processor.getPortableKitsDirectory();
        kitDirectory.createDirectory();
        const auto initialFile = kitDirectory.getChildFile ("New Kit.svkit");

        folderChooser = std::make_unique<juce::FileChooser> (
            "Save SV-Drummer Kit", initialFile, "*.svkit", true);

        folderChooser->launchAsync (
            juce::FileBrowserComponent::saveMode
                | juce::FileBrowserComponent::canSelectFiles
                | juce::FileBrowserComponent::warnAboutOverwriting,
            [this] (const juce::FileChooser& chooser)
            {
                auto resultFile = chooser.getResult();

                if (resultFile == juce::File())
                    return;

                if (! resultFile.hasFileExtension ("svkit"))
                    resultFile = resultFile.withFileExtension ("svkit");

                const auto result = processor.saveKitToFile (resultFile);

                if (result.failed())
                {
                    juce::AlertWindow::showMessageBoxAsync (
                        juce::MessageBoxIconType::WarningIcon,
                        "SV-Drummer Kit Not Saved",
                        result.getErrorMessage());
                    return;
                }

                refresh (false);
                refreshSaveState();
            });
    }

    void choosePatternSaveLocation()
    {
        const int patternIndex = processor.getCurrentPatternIndex();
        auto patternsDirectory = processor.getPortablePatternsDirectory()
                                          .getChildFile ("Patterns");
        patternsDirectory.createDirectory();
        const auto initialFile = patternsDirectory.getChildFile (
            "Pattern " + juce::String (patternIndex + 1).paddedLeft ('0', 2)
                + ".svpattern");

        folderChooser = std::make_unique<juce::FileChooser> (
            "Save SV-Drummer Pattern", initialFile, "*.svpattern", true);

        folderChooser->launchAsync (
            juce::FileBrowserComponent::saveMode
                | juce::FileBrowserComponent::canSelectFiles
                | juce::FileBrowserComponent::warnAboutOverwriting,
            [this, patternIndex] (const juce::FileChooser& chooser)
            {
                auto resultFile = chooser.getResult();

                if (resultFile == juce::File())
                    return;

                if (! resultFile.hasFileExtension ("svpattern"))
                    resultFile = resultFile.withFileExtension ("svpattern");

                const auto result = processor.savePatternSlotToFile (
                    patternIndex, resultFile);

                if (result.failed())
                {
                    juce::AlertWindow::showMessageBoxAsync (
                        juce::MessageBoxIconType::WarningIcon,
                        "SV-Drummer Pattern Not Saved",
                        result.getErrorMessage());
                    return;
                }

                refresh (false);
            });
    }

    void chooseProjectSaveLocation()
    {
        auto projectsDirectory = processor.getPortableProjectsDirectory();
        projectsDirectory.createDirectory();
        const auto initialFile = projectsDirectory.getChildFile (
            "New Project.svproject");

        folderChooser = std::make_unique<juce::FileChooser> (
            "Save SV-Drummer Project", initialFile, "*.svproject", true);

        folderChooser->launchAsync (
            juce::FileBrowserComponent::saveMode
                | juce::FileBrowserComponent::canSelectFiles
                | juce::FileBrowserComponent::warnAboutOverwriting,
            [this] (const juce::FileChooser& chooser)
            {
                auto resultFile = chooser.getResult();

                if (resultFile == juce::File())
                    return;

                if (! resultFile.hasFileExtension ("svproject"))
                    resultFile = resultFile.withFileExtension ("svproject");

                const auto result = processor.saveProjectToFile (resultFile);

                if (result.failed())
                {
                    juce::AlertWindow::showMessageBoxAsync (
                        juce::MessageBoxIconType::WarningIcon,
                        "SV-Drummer Project Not Saved",
                        result.getErrorMessage());
                    return;
                }

                refresh (false);
            });
    }

    void performSave()
    {
        if (onSaveRequested == nullptr)
            return;

        const auto result = onSaveRequested();

        if (result.failed())
        {
            juce::AlertWindow::showMessageBoxAsync (
                juce::MessageBoxIconType::WarningIcon,
                "SV-Drummer Not Saved",
                result.getErrorMessage());
        }

        refreshSaveState();
    }

    SVDrummerAudioProcessor& processor;
    std::function<juce::Result()> onSaveRequested;
    std::function<bool()> hasUnsavedChanges;
    SVDrummerIconButton samplesButton {
        "Samples", SVDrummerIconButton::Icon::samples };
    SVDrummerIconButton kitsButton {
        "Kits", SVDrummerIconButton::Icon::kits };
    SVDrummerIconButton patternsButton {
        "Patterns", SVDrummerIconButton::Icon::patterns };
    SVDrummerIconButton projectsButton {
        "Projects", SVDrummerIconButton::Icon::projects };
    SVDrummerIconButton addFolderButton {
        "Add sample folder", SVDrummerIconButton::Icon::add };
    SVDrummerIconButton removeFolderButton {
        "Remove sample folder", SVDrummerIconButton::Icon::remove };
    SVDrummerIconButton refreshButton {
        "Refresh browser", SVDrummerIconButton::Icon::refresh };
    SVDrummerIconButton previewButton {
        "Preview sample", SVDrummerIconButton::Icon::preview };
    SVDrummerIconButton saveButton {
        "Save library item", SVDrummerIconButton::Icon::save };
    SVDrummerIconButton openFolderButton {
        "Open library folder", SVDrummerIconButton::Icon::openFolder };
    SampleBrowserTree tree;
    juce::Label emptyLabel;
    std::unique_ptr<juce::FileChooser> folderChooser;
    SVDrummerAudioProcessor::BrowserMode mode =
        SVDrummerAudioProcessor::BrowserMode::samples;
    bool initialised = false;
};

class SVDrummerPadComponent final : public juce::Component,
                                    public juce::FileDragAndDropTarget,
                                    public juce::DragAndDropTarget
{
public:
    SVDrummerPadComponent (SVDrummerAudioProcessor& owner,
                           int padNumber,
                           std::function<void (int)> settingsCallback,
                           std::function<void (int)> selectionCallback,
                           std::function<void()> loadCompletedCallback)
        : processor (owner),
          padIndex (padNumber),
          accent (getPadColour (padNumber)),
          onSettingsRequested (std::move (settingsCallback)),
          onSelectionRequested (std::move (selectionCallback)),
          onLoadCompleted (std::move (loadCompletedCallback))
    {
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        lastActivityCounter = processor.getPadActivityCounter (padIndex);
    }

    void paint (juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat();
        const bool muted = processor.isPadMuted (padIndex);
        const bool soloed = processor.isPadSoloed (padIndex);
        const bool empty = processor.getPadSample (padIndex) == nullptr;
        const auto occupiedAccent = empty ? accent.darker (1.22f) : accent;
        const auto effectiveAccent = muted
                                       ? occupiedAccent.withSaturation (0.18f).darker (0.2f)
                                       : occupiedAccent;

        g.setColour (juce::Colour (0xff15181c));
        g.fillRoundedRectangle (bounds, 4.0f);

        if (flashAmount > 0.0f)
        {
            g.setColour (effectiveAccent.withAlpha (0.15f + flashAmount * 0.32f));
            g.fillRoundedRectangle (bounds.reduced (1.0f), 4.0f);
        }

        if (dropHighlight)
        {
            g.setColour (juce::Colours::white.withAlpha (0.12f));
            g.fillRoundedRectangle (bounds.reduced (2.0f), 3.0f);
        }

        g.setColour (effectiveAccent.withAlpha (soloed ? 1.0f : 0.82f));
        g.drawRoundedRectangle (bounds.reduced (0.75f), 4.0f, soloed ? 2.0f : 1.2f);

        const auto footer = getFooterBounds().toFloat();
        g.setColour (effectiveAccent.withAlpha (muted ? 0.22f : 0.82f));
        g.fillRect (footer);

        auto titleRow = getLocalBounds().reduced (7).removeFromTop (22);
        const auto indicatorBounds = getIndicatorBounds().toFloat();
        titleRow.removeFromRight (22);
        titleRow.removeFromRight (4);

        const auto sampleName = processor.getPadDisplayName (padIndex);
        g.setColour (muted ? mutedTextColour : textColour);
        g.setFont (juce::jmax (10.0f, static_cast<float> (getHeight()) * 0.095f));
        g.drawText (sampleName, titleRow,
                    juce::Justification::centredLeft, true);

        if (selected)
        {
            g.setColour (effectiveAccent);
            g.fillEllipse (indicatorBounds);
        }
        else
        {
            g.setColour (juce::Colour (0xff15181c));
            g.fillEllipse (indicatorBounds);
        }

        g.setColour (effectiveAccent.withAlpha (selected ? 1.0f : 0.72f));
        g.drawEllipse (indicatorBounds.reduced (0.6f), selected ? 1.5f : 1.1f);
        g.setColour (selected ? juce::Colours::white
                              : mutedTextColour.brighter (0.08f));
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText (juce::String (padIndex + 1), indicatorBounds.toNearestInt(),
                    juce::Justification::centred, false);

        drawWaveform (g, effectiveAccent);
        drawFooter (g, footer, muted, soloed);
    }

    void mouseDown (const juce::MouseEvent& event) override
    {
        const auto point = event.getPosition();

        if (getIndicatorBounds().contains (point))
        {
            if (onSelectionRequested)
                onSelectionRequested (padIndex);

            return;
        }

        if (noteBounds.contains (point))
        {
            showNoteMenu();
            return;
        }

        if (muteBounds.contains (point))
        {
            processor.setPadMuted (padIndex, ! processor.isPadMuted (padIndex));
            repaint();
            return;
        }

        if (soloBounds.contains (point))
        {
            processor.setPadSoloed (padIndex, ! processor.isPadSoloed (padIndex));
            repaint();
            return;
        }

        if (settingsBounds.contains (point))
        {
            if (onSettingsRequested)
                onSettingsRequested (padIndex);
            return;
        }

        processor.triggerPadFromInterface (padIndex, 1.0f);
    }

    void mouseWheelMove (const juce::MouseEvent& event,
                         const juce::MouseWheelDetails& wheel) override
    {
        if (! noteBounds.contains (event.getPosition()) || wheel.deltaY == 0.0f)
        {
            juce::Component::mouseWheelMove (event, wheel);
            return;
        }

        processor.setPadMidiNote (
            padIndex,
            processor.getPadMidiNote (padIndex) + (wheel.deltaY > 0.0f ? 1 : -1));
        repaint();
    }

    bool isInterestedInFileDrag (const juce::StringArray& files) override
    {
        return files.size() > 0 && isSupportedPath (files[0]);
    }

    void fileDragEnter (const juce::StringArray&, int, int) override
    {
        dropHighlight = true;
        repaint();
    }

    void fileDragExit (const juce::StringArray&) override
    {
        dropHighlight = false;
        repaint();
    }

    void filesDropped (const juce::StringArray& files, int, int) override
    {
        dropHighlight = false;

        if (files.size() > 0)
            loadFile (juce::File (files[0]));
    }

    bool isInterestedInDragSource (const SourceDetails& details) override
    {
        return isSupportedPath (details.description.toString());
    }

    void itemDragEnter (const SourceDetails&) override
    {
        dropHighlight = true;
        repaint();
    }

    void itemDragExit (const SourceDetails&) override
    {
        dropHighlight = false;
        repaint();
    }

    void itemDropped (const SourceDetails& details) override
    {
        dropHighlight = false;
        loadFile (juce::File (details.description.toString()));
    }

    void refreshAnimation()
    {
        const auto currentCounter = processor.getPadActivityCounter (padIndex);

        if (currentCounter != lastActivityCounter)
        {
            lastActivityCounter = currentCounter;
            flashAmount = 1.0f;
        }
        else
        {
            flashAmount = juce::jmax (0.0f, flashAmount - 0.12f);
        }

        repaint();
    }

    void setSelected (bool shouldBeSelected)
    {
        if (selected != shouldBeSelected)
        {
            selected = shouldBeSelected;
            repaint();
        }
    }

private:
    juce::Rectangle<int> getIndicatorBounds() const
    {
        auto titleRow = getLocalBounds().reduced (7).removeFromTop (22);
        return titleRow.removeFromRight (22).withSizeKeepingCentre (20, 20);
    }

    juce::Rectangle<int> getFooterBounds()
    {
        return getLocalBounds().removeFromBottom (juce::jmax (22, getHeight() / 5));
    }

    void drawWaveform (juce::Graphics& g, juce::Colour colour)
    {
        const auto sample = processor.getPadSample (padIndex);
        auto waveformBounds = getLocalBounds().reduced (7);
        waveformBounds.removeFromTop (22);
        waveformBounds.removeFromBottom (getFooterBounds().getHeight() - 4);

        if (sample == nullptr || sample->waveform.empty())
        {
            g.setColour (mutedTextColour.withAlpha (0.55f));
            g.setFont (juce::FontOptions (11.5f, juce::Font::bold));
            g.drawText ("DROP SAMPLE", waveformBounds,
                        juce::Justification::centred, false);
            return;
        }

        const float middle = static_cast<float> (waveformBounds.getCentreY());
        const float halfHeight = static_cast<float> (waveformBounds.getHeight()) * 0.45f;
        const float left = static_cast<float> (waveformBounds.getX());
        const float width = static_cast<float> (waveformBounds.getWidth());
        const int points = static_cast<int> (sample->waveform.size());
        const int columns = juce::jmax (1, juce::jmin (waveformBounds.getWidth(), points));

        g.setColour (colour.withAlpha (0.94f));

        for (int column = 0; column < columns; ++column)
        {
            const int firstPoint = column * points / columns;
            const int lastPoint = juce::jmax (
                firstPoint + 1, (column + 1) * points / columns);
            float minimum = 1.0f;
            float maximum = -1.0f;

            for (int point = firstPoint; point < juce::jmin (points, lastPoint); ++point)
            {
                const auto range = sample->waveform[static_cast<std::size_t> (point)];
                minimum = juce::jmin (minimum, range.getStart());
                maximum = juce::jmax (maximum, range.getEnd());
            }

            const float x = left + width * static_cast<float> (column)
                                   / static_cast<float> (juce::jmax (1, columns - 1));
            g.drawVerticalLine (juce::roundToInt (x),
                                middle - maximum * halfHeight,
                                middle - minimum * halfHeight);
        }

        g.setColour (colour.withAlpha (0.18f));
        g.drawHorizontalLine (static_cast<int> (middle),
                              static_cast<float> (waveformBounds.getX()),
                              static_cast<float> (waveformBounds.getRight()));
    }

    void drawFooter (juce::Graphics& g,
                     juce::Rectangle<float> footer,
                     bool muted,
                     bool soloed)
    {
        auto footerInt = footer.toNearestInt();
        noteBounds = footerInt.removeFromLeft (footerInt.getWidth() * 52 / 100);
        muteBounds = footerInt.removeFromLeft (footerInt.getWidth() * 31 / 100);
        soloBounds = footerInt.removeFromLeft (footerInt.getWidth() / 2);
        settingsBounds = footerInt;

        g.setColour (juce::Colours::black.withAlpha (0.30f));
        g.drawVerticalLine (noteBounds.getRight(), footer.getY(), footer.getBottom());
        g.drawVerticalLine (muteBounds.getRight(), footer.getY(), footer.getBottom());
        g.drawVerticalLine (soloBounds.getRight(), footer.getY(), footer.getBottom());

        g.setFont (juce::FontOptions (
            juce::jmax (10.5f, footer.getHeight() * 0.44f), juce::Font::bold));
        g.setColour (juce::Colours::white.withAlpha (0.94f));
        g.drawText (midiNoteDescription (processor.getPadMidiNote (padIndex)),
                    noteBounds.reduced (3, 0), juce::Justification::centred, true);

        g.setColour (muted ? juce::Colours::white : juce::Colours::white.withAlpha (0.62f));
        g.drawText ("M", muteBounds, juce::Justification::centred, false);
        g.setColour (soloed ? juce::Colours::white : juce::Colours::white.withAlpha (0.62f));
        g.drawText ("S", soloBounds, juce::Justification::centred, false);

        drawCog (g, settingsBounds.toFloat());
    }

    void drawCog (juce::Graphics& g, juce::Rectangle<float> bounds)
    {
        const auto centre = bounds.getCentre();
        const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.26f;
        g.setColour (juce::Colours::white.withAlpha (0.72f));
        g.drawEllipse (centre.x - radius, centre.y - radius,
                       radius * 2.0f, radius * 2.0f, 1.5f);
        g.fillEllipse (centre.x - 1.8f, centre.y - 1.8f, 3.6f, 3.6f);

        for (int tooth = 0; tooth < 8; ++tooth)
        {
            const float angle = juce::MathConstants<float>::twoPi
                              * static_cast<float> (tooth) / 8.0f;
            g.drawLine (centre.x + std::sin (angle) * radius,
                        centre.y - std::cos (angle) * radius,
                        centre.x + std::sin (angle) * radius * 1.38f,
                        centre.y - std::cos (angle) * radius * 1.38f,
                        1.4f);
        }
    }

    void showNoteMenu()
    {
        juce::PopupMenu menu;
        const int currentNote = processor.getPadMidiNote (padIndex);

        for (int group = 0; group < 11; ++group)
        {
            juce::PopupMenu octaveMenu;
            const int firstNote = group * 12;
            const int lastNote = juce::jmin (127, firstNote + 11);

            for (int note = firstNote; note <= lastNote; ++note)
                octaveMenu.addItem (note + 1, midiNoteDescription (note),
                                    true, note == currentNote);

            menu.addSubMenu ("Notes " + juce::String (firstNote)
                                 + "-" + juce::String (lastNote),
                             octaveMenu);
        }

        juce::Component::SafePointer<SVDrummerPadComponent> safeThis (this);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                            [safeThis] (int result)
                            {
                                if (safeThis != nullptr && result > 0)
                                {
                                    safeThis->processor.setPadMidiNote (
                                        safeThis->padIndex, result - 1);
                                    safeThis->repaint();
                                }
                            });
    }

    void loadFile (const juce::File& file)
    {
        if (SVDrummerAudioProcessor::isSupportedProjectFile (file))
        {
            loadProjectWithConfirmation (file);
            return;
        }

        if (SVDrummerAudioProcessor::isSupportedKitFile (file))
        {
            loadKitWithConfirmation (file);
            return;
        }

        const auto result = processor.loadSampleIntoPad (padIndex, file);

        if (result.failed())
        {
            juce::AlertWindow::showMessageBoxAsync (
                juce::MessageBoxIconType::WarningIcon,
                "SV-Drummer",
                "Could not load the sample:\n\n" + result.getErrorMessage());
        }

        repaint();
    }

    void loadProjectWithConfirmation (const juce::File& file)
    {
        if (! processor.kitHasSamples() && ! processor.patternSetHasSteps())
        {
            finishLoadingProject (file);
            return;
        }

        juce::Component::SafePointer<SVDrummerPadComponent> safeThis (this);
        juce::AlertWindow::showOkCancelBox (
            juce::MessageBoxIconType::QuestionIcon,
            "Replace Current Project?",
            "Loading this Project will replace the complete Kit and Pattern Set.\n\n"
                + file.getFileNameWithoutExtension(),
            "Load Project", "Cancel", this,
            juce::ModalCallbackFunction::create (
                [safeThis, file] (int result)
                {
                    if (safeThis != nullptr && result != 0)
                        safeThis->finishLoadingProject (file);
                }));
    }

    void finishLoadingProject (const juce::File& file)
    {
        const auto result = processor.loadProjectFromFile (file);

        if (result.failed())
        {
            juce::AlertWindow::showMessageBoxAsync (
                juce::MessageBoxIconType::WarningIcon,
                "SV-Drummer",
                "Could not load the Project:\n\n" + result.getErrorMessage());
        }
        else if (onLoadCompleted)
        {
            onLoadCompleted();
        }

        repaint();
    }

    void loadKitWithConfirmation (const juce::File& file)
    {
        if (! processor.kitHasSamples())
        {
            finishLoadingKit (file);
            return;
        }

        juce::Component::SafePointer<SVDrummerPadComponent> safeThis (this);
        juce::AlertWindow::showOkCancelBox (
            juce::MessageBoxIconType::QuestionIcon,
            "Replace Current Kit?",
            "Loading this kit will replace all sixteen pads and their settings.\n\n"
                + file.getFileNameWithoutExtension(),
            "Load Kit", "Cancel", this,
            juce::ModalCallbackFunction::create (
                [safeThis, file] (int result)
                {
                    if (safeThis != nullptr && result != 0)
                        safeThis->finishLoadingKit (file);
                }));
    }

    void finishLoadingKit (const juce::File& file)
    {
        const auto result = processor.loadKitFromFile (file);

        if (result.failed())
        {
            juce::AlertWindow::showMessageBoxAsync (
                juce::MessageBoxIconType::WarningIcon,
                "SV-Drummer",
                "Could not load the kit:\n\n" + result.getErrorMessage());
        }
        else if (onLoadCompleted)
        {
            onLoadCompleted();
        }

        repaint();
    }

    SVDrummerAudioProcessor& processor;
    int padIndex = 0;
    juce::Colour accent;
    std::function<void (int)> onSettingsRequested;
    std::function<void (int)> onSelectionRequested;
    std::function<void()> onLoadCompleted;
    juce::Rectangle<int> noteBounds;
    juce::Rectangle<int> muteBounds;
    juce::Rectangle<int> soloBounds;
    juce::Rectangle<int> settingsBounds;
    std::uint64_t lastActivityCounter = 0;
    float flashAmount = 0.0f;
    bool dropHighlight = false;
    bool selected = false;
};

class SVDrummerWaveformEditor final : public juce::Component
{
    enum class Marker
    {
        none,
        sampleStart,
        sampleEnd,
        loopStart,
        loopEnd
    };

public:
    explicit SVDrummerWaveformEditor (SVDrummerAudioProcessor& owner)
        : processor (owner)
    {
    }

    void setPadIndex (int newPadIndex)
    {
        const int newIndex = juce::jlimit (
            0, SVDrummerAudioProcessor::numberOfPads - 1, newPadIndex);

        if (padIndex != newIndex)
        {
            padIndex = newIndex;
            resetView();
            displayedSample.reset();
        }

        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat();
        g.setColour (raisedPanelColour);
        g.fillRoundedRectangle (bounds, 4.0f);
        g.setColour (lineColour);
        g.drawRoundedRectangle (bounds.reduced (0.5f), 4.0f, 1.0f);

        const float sampleStart = markerPosition (Marker::sampleStart);
        const float sampleEnd = markerPosition (Marker::sampleEnd);
        const bool loopEnabled = processor.isPadLoopEnabled (padIndex);
        const float loopStart = markerPosition (Marker::loopStart);
        const float loopEnd = markerPosition (Marker::loopEnd);
        const auto drawMarkerSetting = [&] (Marker marker,
                                             const juce::String& label,
                                             juce::Colour colour,
                                             bool enabled)
        {
            const auto settingBounds = markerSettingBounds (marker);
            auto labelBounds = settingBounds;
            auto valueBounds = markerValueBounds (marker);
            labelBounds.setRight (valueBounds.getX() - 4.0f);

            g.setColour (enabled ? colour : mutedTextColour.darker (0.20f));
            g.setFont (juce::FontOptions (11.5f, juce::Font::bold));
            g.drawText (label, labelBounds.toNearestInt(),
                        juce::Justification::centredRight, false);
            g.setColour (raisedPanelColour.darker (0.24f));
            g.fillRoundedRectangle (valueBounds, 2.5f);
            g.setColour ((enabled ? colour : mutedTextColour).withAlpha (0.72f));
            g.drawRoundedRectangle (valueBounds.reduced (0.5f), 2.5f, 1.0f);
            g.setColour (enabled ? textColour : mutedTextColour.darker (0.16f));
            g.setFont (juce::FontOptions (12.5f, juce::Font::bold));
            g.drawText (juce::String (markerSamplePosition (marker)),
                        valueBounds.toNearestInt(),
                        juce::Justification::centred, false);
        };

        drawMarkerSetting (Marker::sampleStart, "START", trimMarkerColour, true);
        drawMarkerSetting (Marker::sampleEnd, "END", trimMarkerColour, true);
        drawMarkerSetting (Marker::loopStart, "L-START", loopMarkerColour,
                           loopEnabled);
        drawMarkerSetting (Marker::loopEnd, "L-END", loopMarkerColour,
                           loopEnabled);

        const auto waveformBounds = getWaveformBounds();
        const auto overviewBounds = getOverviewBounds();
        const auto sample = processor.getPadSample (padIndex);

        if (sample != displayedSample)
        {
            displayedSample = sample;
            resetView();
        }

        if (sample == nullptr || sample->waveform.empty() || waveformBounds.isEmpty())
        {
            g.setColour (mutedTextColour);
            g.setFont (12.0f);
            g.drawText ("NO SAMPLE LOADED", waveformBounds,
                        juce::Justification::centred);
            return;
        }

        const auto accent = getPadColour (padIndex);
        const float visibleSelectionStart = juce::jmax (viewStart, sampleStart);
        const float visibleSelectionEnd = juce::jmin (viewEnd, sampleEnd);

        if (visibleSelectionEnd > visibleSelectionStart)
        {
            const float selectedStartX = samplePositionToX (
                visibleSelectionStart, waveformBounds);
            const float selectedEndX = samplePositionToX (
                visibleSelectionEnd, waveformBounds);
            g.setColour (accent.withAlpha (0.09f));
            g.fillRect (juce::Rectangle<float> (
                selectedStartX, waveformBounds.getY(),
                juce::jmax (0.0f, selectedEndX - selectedStartX),
                waveformBounds.getHeight()));
        }

        if (loopEnabled)
        {
            const float visibleLoopStart = juce::jmax (viewStart, loopStart);
            const float visibleLoopEnd = juce::jmin (viewEnd, loopEnd);

            if (visibleLoopEnd > visibleLoopStart)
            {
                const float loopStartX = samplePositionToX (
                    visibleLoopStart, waveformBounds);
                const float loopEndX = samplePositionToX (
                    visibleLoopEnd, waveformBounds);
                const auto loopArea = juce::Rectangle<float> (
                    loopStartX, waveformBounds.getY(),
                    juce::jmax (0.0f, loopEndX - loopStartX),
                    waveformBounds.getHeight());
                g.setColour (loopMarkerColour.withAlpha (0.10f));
                g.fillRect (loopArea);
                g.setColour (loopMarkerColour.withAlpha (0.54f));
                g.drawHorizontalLine (
                    juce::roundToInt (waveformBounds.getY() + 1.0f),
                    loopArea.getX(), loopArea.getRight());
            }
        }

        g.setColour (lineColour.withAlpha (0.34f));

        for (int guide = 1; guide < 4; ++guide)
        {
            const float x = waveformBounds.getX()
                          + waveformBounds.getWidth() * static_cast<float> (guide) / 4.0f;
            g.drawVerticalLine (juce::roundToInt (x),
                                waveformBounds.getY(), waveformBounds.getBottom());
        }

        drawFilledWaveform (g, *sample, waveformBounds,
                            viewStart, viewEnd, accent, 0.42f, 0.94f);

        g.setColour (backgroundColour.withAlpha (0.58f));

        if (sampleStart > viewStart)
            g.fillRect (waveformBounds.withRight (
                juce::jmin (waveformBounds.getRight(),
                            samplePositionToX (sampleStart, waveformBounds))));

        if (sampleEnd < viewEnd)
            g.fillRect (waveformBounds.withLeft (
                juce::jmax (waveformBounds.getX(),
                            samplePositionToX (sampleEnd, waveformBounds))));

        g.setColour (juce::Colours::white.withAlpha (0.42f));
        g.drawLine (waveformBounds.getX(), waveformBounds.getCentreY(),
                    waveformBounds.getRight(), waveformBounds.getCentreY(),
                    1.15f);

        drawAmplitudeEnvelope (g, *sample, waveformBounds,
                               sampleStart, sampleEnd, accent);

        const auto drawMarker = [&] (Marker marker,
                                     const juce::String& text,
                                     juce::Colour markerColour)
        {
            const float samplePosition = markerPosition (marker);

            if (samplePosition < viewStart || samplePosition > viewEnd)
                return;

            const float markerX = samplePositionToX (samplePosition, waveformBounds);
            const auto flag = markerFlagBounds (marker);
            g.setColour (markerColour);
            g.drawVerticalLine (juce::roundToInt (markerX),
                                waveformBounds.getY(), waveformBounds.getBottom());
            g.fillRoundedRectangle (flag, 2.0f);
            g.setColour (juce::Colours::white.withAlpha (0.96f));
            g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
            g.drawText (text, flag, juce::Justification::centred);
        };

        drawMarker (Marker::sampleStart, "S", trimMarkerColour);
        drawMarker (Marker::sampleEnd, "E", trimMarkerColour);

        if (loopEnabled)
        {
            drawMarker (Marker::loopStart, "LS", loopMarkerColour);
            drawMarker (Marker::loopEnd, "LE", loopMarkerColour);
        }

        g.setColour (backgroundColour.darker (0.18f));
        g.fillRoundedRectangle (overviewBounds, 2.0f);
        g.setColour (lineColour.withAlpha (0.64f));
        g.drawRoundedRectangle (overviewBounds.reduced (0.5f), 2.0f, 1.0f);
        drawFilledWaveform (g, *sample, overviewBounds.reduced (2.0f),
                            0.0f, 1.0f,
                            mutedTextColour.brighter (0.15f), 0.35f, 0.62f);

        const auto viewport = getViewportBounds();
        g.setColour (accent.withAlpha (0.12f));
        g.fillRect (viewport);
        g.setColour (accent.withAlpha (0.92f));
        g.drawRect (viewport, 1.2f);
        g.setColour (juce::Colours::white.withAlpha (0.46f));
        g.drawLine (overviewBounds.getX(), overviewBounds.getCentreY(),
                    overviewBounds.getRight(), overviewBounds.getCentreY(),
                    1.0f);

        constexpr float handleWidth = 3.0f;
        const auto leftHandle = juce::Rectangle<float> (
            viewport.getX() - handleWidth * 0.5f,
            overviewBounds.getY(), handleWidth, overviewBounds.getHeight());
        const auto rightHandle = juce::Rectangle<float> (
            viewport.getRight() - handleWidth * 0.5f,
            overviewBounds.getY(), handleWidth, overviewBounds.getHeight());
        g.setColour (juce::Colours::white.withAlpha (0.72f));
        g.fillRoundedRectangle (leftHandle, 1.0f);
        g.fillRoundedRectangle (rightHandle, 1.0f);
    }

    void mouseMove (const juce::MouseEvent& event) override
    {
        if (processor.getPadSample (padIndex) == nullptr)
        {
            setMouseCursor (juce::MouseCursor::NormalCursor);
            return;
        }

        if (markerValueAt (event.position) != Marker::none)
        {
            setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
            return;
        }

        if (getOverviewBounds().contains (event.position))
        {
            setMouseCursor (juce::MouseCursor::DraggingHandCursor);
            return;
        }

        if (getWaveformBounds().contains (event.position))
        {
            setMouseCursor (markerAt (event.position) != Marker::none
                                ? juce::MouseCursor::LeftRightResizeCursor
                                : juce::MouseCursor::PointingHandCursor);
            return;
        }

        setMouseCursor (juce::MouseCursor::NormalCursor);
    }

    void mouseDown (const juce::MouseEvent& event) override
    {
        if (processor.getPadSample (padIndex) == nullptr
            || ! event.mods.isLeftButtonDown())
            return;

        const auto overview = getOverviewBounds();

        if (overview.contains (event.position))
        {
            draggingMarker = false;
            activeMarker = Marker::none;
            draggingOverview = true;
            const auto overviewContent = getOverviewContentBounds();

            const float relativeMouse = juce::jlimit (
                0.0f, 1.0f,
                (event.position.x - overviewContent.getX())
                    / overviewContent.getWidth());
            const float viewLength = viewEnd - viewStart;

            if (getViewportBounds().contains (event.position))
            {
                overviewDragOffset = relativeMouse - viewStart;
            }
            else
            {
                overviewDragOffset = viewLength * 0.5f;
                moveViewToOverviewPosition (relativeMouse);
            }

            repaint();
            return;
        }

        const auto bounds = getWaveformBounds();

        if (! bounds.contains (event.position))
            return;

        if (event.getNumberOfClicks() > 1)
            return;

        activeMarker = markerAt (event.position);

        if (activeMarker != Marker::none)
        {
            draggingMarker = true;
            updateMarker (event.position.x);
        }
        else
        {
            draggingMarker = false;
            processor.triggerPadFromInterface (padIndex, 1.0f);
        }
    }

    void mouseDrag (const juce::MouseEvent& event) override
    {
        if (draggingOverview)
        {
            const auto overview = getOverviewContentBounds();
            const float relativeMouse = juce::jlimit (
                0.0f, 1.0f,
                (event.position.x - overview.getX()) / overview.getWidth());
            moveViewToOverviewPosition (relativeMouse);
        }
        else if (draggingMarker)
            updateMarker (event.position.x);
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        draggingMarker = false;
        draggingOverview = false;
        activeMarker = Marker::none;
    }

    void mouseWheelMove (const juce::MouseEvent& event,
                         const juce::MouseWheelDetails& wheel) override
    {
        const auto valueMarker = markerValueAt (event.position);

        if (valueMarker != Marker::none && wheel.deltaY != 0.0f)
        {
            nudgeMarker (valueMarker, wheel.deltaY > 0.0f ? 1 : -1);
            repaint();
            return;
        }

        const auto bounds = getWaveformBounds();

        if (processor.getPadSample (padIndex) == nullptr
            || wheel.deltaY == 0.0f
            || ! bounds.contains (event.position))
        {
            juce::Component::mouseWheelMove (event, wheel);
            return;
        }

        const float oldLength = viewEnd - viewStart;
        const float newLength = juce::jlimit (
            minimumViewLength, 1.0f,
            oldLength * (wheel.deltaY > 0.0f ? 0.80f : 1.25f));
        const float relativeMouse = juce::jlimit (
            0.0f, 1.0f,
            (event.position.x - bounds.getX()) / bounds.getWidth());
        const float anchor = viewStart + oldLength * relativeMouse;
        viewStart = juce::jlimit (
            0.0f, 1.0f - newLength, anchor - newLength * relativeMouse);
        viewEnd = viewStart + newLength;

        if (newLength > 0.995f)
            resetView();

        repaint();
    }

    void mouseDoubleClick (const juce::MouseEvent& event) override
    {
        if (! getWaveformBounds().contains (event.position)
            && ! getOverviewBounds().contains (event.position))
            return;

        processor.resetPadSampleMarkers (padIndex);
        resetView();
        repaint();
    }

private:
    juce::Rectangle<float> getMarkerHeaderBounds() const
    {
        auto area = getLocalBounds().toFloat().reduced (8.0f);
        return area.removeFromTop (markerHeaderHeight);
    }

    juce::Rectangle<float> markerSettingBounds (Marker marker) const
    {
        const int index = marker == Marker::sampleStart ? 0
                        : marker == Marker::sampleEnd ? 1
                        : marker == Marker::loopStart ? 2 : 3;
        const auto header = getMarkerHeaderBounds();
        const float settingWidth = header.getWidth() * 0.25f;
        return { header.getX() + settingWidth * static_cast<float> (index),
                 header.getY(), settingWidth, header.getHeight() };
    }

    juce::Rectangle<float> markerValueBounds (Marker marker) const
    {
        auto setting = markerSettingBounds (marker).reduced (3.0f, 2.0f);
        const float valueWidth = juce::jlimit (
            74.0f, 108.0f, setting.getWidth() * 0.52f);
        return setting.removeFromRight (valueWidth);
    }

    juce::Rectangle<float> getWaveformBounds() const
    {
        auto area = getLocalBounds().toFloat().reduced (8.0f);
        area.removeFromTop (markerHeaderHeight + 2.0f);
        area.removeFromBottom (overviewHeight + overviewGap);
        return area;
    }

    juce::Rectangle<float> getOverviewBounds() const
    {
        auto area = getLocalBounds().toFloat().reduced (8.0f);
        area.removeFromTop (markerHeaderHeight + 2.0f);
        return area.removeFromBottom (overviewHeight);
    }

    juce::Rectangle<float> getOverviewContentBounds() const
    {
        return getOverviewBounds().reduced (2.0f);
    }

    juce::Rectangle<float> getViewportBounds() const
    {
        const auto overview = getOverviewContentBounds();
        return { overview.getX() + overview.getWidth() * viewStart,
                 overview.getY(),
                 overview.getWidth() * (viewEnd - viewStart),
                 overview.getHeight() };
    }

    static void drawFilledWaveform (
        juce::Graphics& g,
        const SVDrummerAudioProcessor::SampleData& sample,
        juce::Rectangle<float> bounds,
        float normalisedStart,
        float normalisedEnd,
        juce::Colour colour,
        float fillAlpha,
        float lineAlpha)
    {
        const auto& audio = sample.audio;
        const int sampleCount = audio.getNumSamples();
        const int channelCount = audio.getNumChannels();

        if (sampleCount <= 0 || channelCount <= 0 || bounds.isEmpty())
            return;

        const float start = juce::jlimit (0.0f, 1.0f, normalisedStart);
        const float end = juce::jlimit (start, 1.0f, normalisedEnd);
        const int pointCount = juce::jmax (
            2, juce::roundToInt (bounds.getWidth() * 2.0f));
        const float centreY = bounds.getCentreY();
        const float halfHeight = bounds.getHeight() * 0.46f;

        juce::Path fillPath;
        juce::Path linePath;
        float finalX = bounds.getX();

        for (int point = 0; point < pointCount; ++point)
        {
            const float position = static_cast<float> (point)
                                 / static_cast<float> (pointCount - 1);
            const double sourcePosition = static_cast<double> (
                start + (end - start) * position)
                * static_cast<double> (sampleCount - 1);
            const int firstSample = juce::jlimit (
                0, sampleCount - 1,
                static_cast<int> (std::floor (sourcePosition)));
            const int secondSample = juce::jmin (sampleCount - 1,
                                                 firstSample + 1);
            const float fraction = static_cast<float> (
                sourcePosition - static_cast<double> (firstSample));
            float value = 0.0f;

            for (int channel = 0; channel < channelCount; ++channel)
            {
                value += juce::jmap (
                    fraction,
                    audio.getSample (channel, firstSample),
                    audio.getSample (channel, secondSample));
            }

            value = juce::jlimit (
                -1.0f, 1.0f, value / static_cast<float> (channelCount));
            const float x = bounds.getX() + bounds.getWidth() * position;
            const float y = centreY - value * halfHeight;

            if (point == 0)
            {
                fillPath.startNewSubPath (x, centreY);
                fillPath.lineTo (x, y);
                linePath.startNewSubPath (x, y);
            }
            else
            {
                fillPath.lineTo (x, y);
                linePath.lineTo (x, y);
            }

            finalX = x;
        }

        fillPath.lineTo (finalX, centreY);
        fillPath.closeSubPath();

        g.setColour (colour.withAlpha (fillAlpha));
        g.fillPath (fillPath);
        g.setColour (colour.withAlpha (lineAlpha));
        g.strokePath (linePath, juce::PathStrokeType (1.0f));
    }

    void drawAmplitudeEnvelope (
        juce::Graphics& g,
        const SVDrummerAudioProcessor::SampleData& sample,
        juce::Rectangle<float> bounds,
        float normalisedStart,
        float normalisedEnd,
        juce::Colour accent) const
    {
        const float rangeLength = juce::jmax (
            1.0f / static_cast<float> (
                juce::jmax (1, sample.audio.getNumSamples() - 1)),
            normalisedEnd - normalisedStart);
        const double sourceSamples = rangeLength
                                   * static_cast<double> (
                                         juce::jmax (1, sample.audio.getNumSamples() - 1));
        const double tuneRatio = std::pow (
            2.0, static_cast<double> (
                     processor.getPadTuneSemitones (padIndex)) / 12.0);
        const double durationMs = sourceSamples
                                / juce::jmax (1.0, sample.sourceSampleRate * tuneRatio)
                                * 1000.0;
        const double attackMs = processor.getPadAmpAttackMs (padIndex);
        const double decayMs = processor.getPadAmpDecayMs (padIndex);
        const double releaseMs = processor.getPadAmpReleaseMs (padIndex);
        const float sustain = juce::jlimit (
            0.0f, 1.0f, processor.getPadAmpSustain (padIndex));
        const float attackEnd = juce::jlimit (
            0.0f, 1.0f,
            static_cast<float> (attackMs / juce::jmax (1.0, durationMs)));
        const float decayEnd = juce::jlimit (
            attackEnd, 1.0f,
            attackEnd + static_cast<float> (
                decayMs / juce::jmax (1.0, durationMs)));
        const float releaseStart = juce::jlimit (
            decayEnd, 1.0f,
            juce::jmax (decayEnd,
                        1.0f - static_cast<float> (
                            releaseMs / juce::jmax (1.0, durationMs))));
        const auto envelopePosition = [normalisedStart, rangeLength] (float phase)
        {
            return normalisedStart + rangeLength * phase;
        };
        const auto envelopePoint = [this, bounds, &envelopePosition] (
            float phase, float level)
        {
            constexpr float verticalPadding = 6.0f;
            return juce::Point<float> (
                samplePositionToX (envelopePosition (phase), bounds),
                bounds.getBottom() - verticalPadding
                    - juce::jlimit (0.0f, 1.0f, level)
                        * (bounds.getHeight() - verticalPadding * 2.0f));
        };

        const auto startPoint = envelopePoint (
            0.0f, attackMs > 0.0 ? 0.0f : 1.0f);
        const auto attackPoint = envelopePoint (attackEnd, 1.0f);
        const auto decayPoint = envelopePoint (decayEnd, sustain);
        const auto sustainPoint = envelopePoint (releaseStart, sustain);
        const auto releasePoint = envelopePoint (1.0f, 0.0f);
        juce::Path envelope;
        envelope.startNewSubPath (startPoint);
        envelope.lineTo (attackPoint);
        envelope.lineTo (decayPoint);
        envelope.lineTo (sustainPoint);
        envelope.lineTo (releasePoint);

        juce::Graphics::ScopedSaveState state (g);
        g.reduceClipRegion (bounds.toNearestInt());
        g.setColour (accent.brighter (0.32f).withAlpha (0.28f));
        g.strokePath (envelope, juce::PathStrokeType (
            4.0f, juce::PathStrokeType::curved,
            juce::PathStrokeType::rounded));
        g.setColour (juce::Colours::white.withAlpha (0.90f));
        g.strokePath (envelope, juce::PathStrokeType (
            1.35f, juce::PathStrokeType::curved,
            juce::PathStrokeType::rounded));

        for (const auto point : { attackPoint, decayPoint,
                                  sustainPoint, releasePoint })
        {
            g.setColour (accent.brighter (0.30f).withAlpha (0.96f));
            g.fillEllipse (point.x - 2.8f, point.y - 2.8f, 5.6f, 5.6f);
            g.setColour (juce::Colours::white.withAlpha (0.82f));
            g.drawEllipse (point.x - 2.8f, point.y - 2.8f, 5.6f, 5.6f, 0.8f);
        }

    }

    void moveViewToOverviewPosition (float relativeMouse)
    {
        const float viewLength = viewEnd - viewStart;
        viewStart = juce::jlimit (
            0.0f, 1.0f - viewLength,
            relativeMouse - overviewDragOffset);
        viewEnd = viewStart + viewLength;
        repaint();
    }

    void updateMarker (float mouseX)
    {
        const auto bounds = getWaveformBounds();

        if (bounds.isEmpty())
            return;

        const float normalisedPosition = juce::jlimit (
            0.0f, 1.0f,
            viewStart + (viewEnd - viewStart)
                            * (mouseX - bounds.getX()) / bounds.getWidth());
        const int lastSample = juce::jmax (
            0, processor.getPadSampleLength (padIndex) - 1);
        const int samplePosition = juce::jlimit (
            0, lastSample,
            juce::roundToInt (
                normalisedPosition * static_cast<float> (lastSample)));

        switch (activeMarker)
        {
            case Marker::sampleStart:
                processor.setPadSampleStart (padIndex, samplePosition);
                break;
            case Marker::sampleEnd:
                processor.setPadSampleEnd (padIndex, samplePosition);
                break;
            case Marker::loopStart:
                processor.setPadLoopStart (padIndex, samplePosition);
                break;
            case Marker::loopEnd:
                processor.setPadLoopEnd (padIndex, samplePosition);
                break;
            case Marker::none:
            default:
                break;
        }

        repaint();
    }

    Marker markerValueAt (juce::Point<float> position) const
    {
        for (const auto marker : { Marker::sampleStart, Marker::sampleEnd,
                                   Marker::loopStart, Marker::loopEnd })
            if (markerValueBounds (marker).contains (position))
                return marker;

        return Marker::none;
    }

    void nudgeMarker (Marker marker, int direction)
    {
        const int amount = direction >= 0 ? 1 : -1;
        const int requested = markerSamplePosition (marker) + amount;

        switch (marker)
        {
            case Marker::sampleStart:
                processor.setPadSampleStart (
                    padIndex, requested, amount);
                break;
            case Marker::sampleEnd:
                processor.setPadSampleEnd (
                    padIndex, requested, amount);
                break;
            case Marker::loopStart:
                processor.setPadLoopStart (
                    padIndex, requested, amount);
                break;
            case Marker::loopEnd:
                processor.setPadLoopEnd (
                    padIndex, requested, amount);
                break;
            case Marker::none:
            default:
                break;
        }
    }

    int markerSamplePosition (Marker marker) const
    {
        switch (marker)
        {
            case Marker::sampleStart:
                return processor.getPadSampleStart (padIndex);
            case Marker::sampleEnd:
                return processor.getPadSampleEnd (padIndex);
            case Marker::loopStart:
                return processor.getPadLoopStart (padIndex);
            case Marker::loopEnd:
                return processor.getPadLoopEnd (padIndex);
            case Marker::none:
            default:
                return 0;
        }
    }

    float markerPosition (Marker marker) const
    {
        const int lastSample = processor.getPadSampleLength (padIndex) - 1;

        if (lastSample <= 0)
            return 0.0f;

        return static_cast<float> (markerSamplePosition (marker))
             / static_cast<float> (lastSample);
    }

    juce::Rectangle<float> markerFlagBounds (Marker marker) const
    {
        if (marker == Marker::none
            || ((marker == Marker::loopStart || marker == Marker::loopEnd)
                && ! processor.isPadLoopEnabled (padIndex)))
            return {};

        const auto waveformBounds = getWaveformBounds();
        const float position = markerPosition (marker);

        if (position < viewStart || position > viewEnd)
            return {};

        const float markerX = samplePositionToX (position, waveformBounds);
        const bool startMarker = marker == Marker::sampleStart
                              || marker == Marker::loopStart;
        const bool topMarker = marker == Marker::loopStart
                            || marker == Marker::loopEnd;
        const float flagX = startMarker
                              ? juce::jmin (
                                    markerX,
                                    waveformBounds.getRight() - markerFlagWidth)
                              : juce::jmax (
                                    waveformBounds.getX(),
                                    markerX - markerFlagWidth);

        return { flagX,
                 topMarker ? waveformBounds.getY()
                           : waveformBounds.getBottom() - markerFlagHeight,
                 markerFlagWidth,
                 markerFlagHeight };
    }

    Marker markerAt (juce::Point<float> position) const
    {
        const auto bounds = getWaveformBounds();
        Marker closestFlag = Marker::none;
        float closestFlagDistance = (std::numeric_limits<float>::max)();

        for (const auto marker : { Marker::sampleStart, Marker::sampleEnd,
                                   Marker::loopStart, Marker::loopEnd })
        {
            if (! markerFlagBounds (marker).contains (position))
                continue;

            const float distance = std::abs (
                position.x - samplePositionToX (markerPosition (marker), bounds));

            if (distance < closestFlagDistance)
            {
                closestFlagDistance = distance;
                closestFlag = marker;
            }
        }

        if (closestFlag != Marker::none)
            return closestFlag;

        const bool chooseLoopMarker = processor.isPadLoopEnabled (padIndex)
                                   && position.y < bounds.getCentreY();

        if (chooseLoopMarker)
        {
            const float loopStartDistance = std::abs (
                position.x - samplePositionToX (
                    markerPosition (Marker::loopStart), bounds));
            const float loopEndDistance = std::abs (
                position.x - samplePositionToX (
                    markerPosition (Marker::loopEnd), bounds));

            if (juce::jmin (loopStartDistance, loopEndDistance)
                <= markerGrabWidth)
            {
                return loopStartDistance <= loopEndDistance
                         ? Marker::loopStart : Marker::loopEnd;
            }
        }

        const float startX = samplePositionToX (
            markerPosition (Marker::sampleStart), bounds);
        const float endX = samplePositionToX (
            markerPosition (Marker::sampleEnd), bounds);
        const float startDistance = std::abs (position.x - startX);
        const float endDistance = std::abs (position.x - endX);

        if (juce::jmin (startDistance, endDistance) <= markerGrabWidth)
            return startDistance <= endDistance
                     ? Marker::sampleStart : Marker::sampleEnd;

        return Marker::none;
    }

    float samplePositionToX (float samplePosition,
                             juce::Rectangle<float> bounds) const
    {
        return bounds.getX()
             + bounds.getWidth() * (samplePosition - viewStart)
                                   / juce::jmax (minimumViewLength,
                                                 viewEnd - viewStart);
    }

    void resetView()
    {
        viewStart = 0.0f;
        viewEnd = 1.0f;
    }

    SVDrummerAudioProcessor& processor;
    static constexpr float markerGrabWidth = 9.0f;
    static constexpr float markerFlagWidth = 22.0f;
    static constexpr float markerFlagHeight = 14.0f;
    static constexpr float markerHeaderHeight = 24.0f;
    static constexpr float minimumViewLength = 0.01f;
    static constexpr float overviewHeight = 28.0f;
    static constexpr float overviewGap = 5.0f;
    std::shared_ptr<const SVDrummerAudioProcessor::SampleData> displayedSample;
    int padIndex = 0;
    float viewStart = 0.0f;
    float viewEnd = 1.0f;
    float overviewDragOffset = 0.5f;
    Marker activeMarker = Marker::none;
    bool draggingMarker = false;
    bool draggingOverview = false;
};

class SVDrummerPadSettingsPanel final : public juce::Component
{
public:
    explicit SVDrummerPadSettingsPanel (SVDrummerAudioProcessor& owner)
        : processor (owner), waveformEditor (owner)
    {
        configureSlider (chokeSlider, chokeLabel, "CHOKE", 0.0,
                         static_cast<double> (SVDrummerAudioProcessor::numberOfPads),
                         1.0);
        chokeSlider.setNumDecimalPlacesToDisplay (0);
        chokeSlider.textFromValueFunction = [] (double value)
        {
            const int group = juce::roundToInt (value);
            return group <= 0 ? juce::String ("OFF") : juce::String (group);
        };

        configureSlider (attackSlider, attackLabel, "ATTACK", 0.0, 2000.0, 1.0);
        configureSlider (decaySlider, decayLabel, "DECAY", 0.0, 5000.0, 1.0);
        configureSlider (sustainSlider, sustainLabel, "SUSTAIN", 0.0, 1.0, 0.01);
        configureSlider (releaseSlider, releaseLabel, "RELEASE", 0.0, 5000.0, 1.0);
        attackSlider.setMouseDragSensitivity (450);
        decaySlider.setMouseDragSensitivity (450);
        sustainSlider.setMouseDragSensitivity (300);
        releaseSlider.setMouseDragSensitivity (450);
        attackSlider.setWheelStep (5.0);
        decaySlider.setWheelStep (10.0);
        sustainSlider.setWheelStep (0.01);
        releaseSlider.setWheelStep (10.0);

        const auto formatEnvelopeTime = [] (double value)
        {
            if (value >= 1000.0)
                return juce::String (value / 1000.0, 2) + " s";

            return juce::String (juce::roundToInt (value)) + " ms";
        };

        attackSlider.textFromValueFunction = formatEnvelopeTime;
        decaySlider.textFromValueFunction = formatEnvelopeTime;
        releaseSlider.textFromValueFunction = formatEnvelopeTime;
        sustainSlider.textFromValueFunction = [] (double value)
        {
            return juce::String (juce::roundToInt (value * 100.0)) + "%";
        };
        attackSlider.updateText();
        decaySlider.updateText();
        sustainSlider.updateText();
        releaseSlider.updateText();
        chokeSlider.onValueChange = [this]
        {
            if (! updating)
                processor.setPadChokeGroup (
                    padIndex, juce::roundToInt (chokeSlider.getValue()));
        };
        attackSlider.onValueChange = [this]
        {
            if (! updating)
            {
                processor.setPadAmpAttackMs (
                    padIndex, static_cast<float> (attackSlider.getValue()));
                waveformEditor.repaint();
            }
        };
        decaySlider.onValueChange = [this]
        {
            if (! updating)
            {
                processor.setPadAmpDecayMs (
                    padIndex, static_cast<float> (decaySlider.getValue()));
                waveformEditor.repaint();
            }
        };
        sustainSlider.onValueChange = [this]
        {
            if (! updating)
            {
                processor.setPadAmpSustain (
                    padIndex, static_cast<float> (sustainSlider.getValue()));
                waveformEditor.repaint();
            }
        };
        releaseSlider.onValueChange = [this]
        {
            if (! updating)
            {
                processor.setPadAmpReleaseMs (
                    padIndex, static_cast<float> (releaseSlider.getValue()));
                waveformEditor.repaint();
            }
        };

        reverseButton.setClickingTogglesState (true);
        reverseButton.setColour (juce::TextButton::buttonColourId,
                                 raisedPanelColour.darker (0.18f));
        reverseButton.setColour (juce::TextButton::buttonOnColourId,
                                 trimMarkerColour.darker (0.30f));
        reverseButton.setColour (juce::TextButton::textColourOffId,
                                 mutedTextColour);
        reverseButton.setColour (juce::TextButton::textColourOnId,
                                 juce::Colours::white);
        reverseButton.onClick = [this]
        {
            if (! updating)
                processor.setPadReversed (padIndex, reverseButton.getToggleState());
        };
        loopButton.setClickingTogglesState (true);
        loopButton.setColour (juce::TextButton::buttonColourId,
                              raisedPanelColour.darker (0.18f));
        loopButton.setColour (juce::TextButton::buttonOnColourId,
                              loopMarkerColour);
        loopButton.setColour (juce::TextButton::textColourOffId,
                              mutedTextColour);
        loopButton.setColour (juce::TextButton::textColourOnId,
                              juce::Colours::white);
        loopButton.onClick = [this]
        {
            if (! updating)
            {
                processor.setPadLoopEnabled (padIndex,
                                             loopButton.getToggleState());
                waveformEditor.repaint();
            }
        };
        snapButton.setClickingTogglesState (true);
        snapButton.setColour (juce::TextButton::buttonColourId,
                              raisedPanelColour.darker (0.18f));
        snapButton.setColour (juce::TextButton::buttonOnColourId,
                              loopMarkerColour.darker (0.42f));
        snapButton.setColour (juce::TextButton::textColourOffId,
                              mutedTextColour);
        snapButton.setColour (juce::TextButton::textColourOnId,
                              juce::Colours::white);
        snapButton.onClick = [this]
        {
            if (! updating)
            {
                processor.setSampleMarkerSnapEnabled (
                    snapButton.getToggleState());
                waveformEditor.repaint();
            }
        };
        clearButton.onClick = [this]
        {
            if (processor.getPadSample (padIndex) == nullptr)
                return;

            const int padToClear = padIndex;
            juce::Component::SafePointer<SVDrummerPadSettingsPanel> safeThis (this);
            juce::AlertWindow::showOkCancelBox (
                juce::MessageBoxIconType::QuestionIcon,
                "Clear Drum Pad",
                "Remove the sample from Pad " + juce::String (padToClear + 1)
                    + "?\n\nThe original audio file will not be deleted.",
                "Clear", "Cancel", this,
                juce::ModalCallbackFunction::create (
                    [safeThis, padToClear] (int result)
                    {
                        if (safeThis == nullptr || result == 0)
                            return;

                        safeThis->processor.clearPadSample (padToClear);

                        if (safeThis->padIndex == padToClear)
                            safeThis->syncFromProcessor();
                    }));
        };
        clearButton.setColour (juce::TextButton::buttonColourId,
                               raisedPanelColour.darker (0.12f));
        clearButton.setColour (juce::TextButton::textColourOffId,
                               juce::Colour (0xffe99696));

        addAndMakeVisible (titleLabel);
        addAndMakeVisible (pathLabel);
        addAndMakeVisible (waveformEditor);
        addAndMakeVisible (reverseButton);
        addAndMakeVisible (loopButton);
        addAndMakeVisible (snapButton);
        addAndMakeVisible (clearButton);

        titleLabel.setColour (juce::Label::textColourId, textColour);
        titleLabel.setFont (juce::FontOptions (18.0f, juce::Font::bold));
        titleLabel.setMinimumHorizontalScale (0.72f);
        pathLabel.setColour (juce::Label::textColourId, mutedTextColour);
        pathLabel.setJustificationType (juce::Justification::centredRight);
        pathLabel.setFont (juce::FontOptions (10.5f));
        pathLabel.setMinimumHorizontalScale (0.55f);

        setPadIndex (0);
    }

    void setPadIndex (int newPadIndex)
    {
        padIndex = juce::jlimit (0, SVDrummerAudioProcessor::numberOfPads - 1,
                                newPadIndex);
        waveformEditor.setPadIndex (padIndex);
        syncFromProcessor();
        repaint();
    }

    void syncFromProcessor()
    {
        const juce::ScopedValueSetter<bool> setter (updating, true);
        refreshPatternResetValues();
        chokeSlider.setValue (processor.getPadChokeGroup (padIndex),
                              juce::dontSendNotification);
        attackSlider.setValue (processor.getPadAmpAttackMs (padIndex),
                               juce::dontSendNotification);
        decaySlider.setValue (processor.getPadAmpDecayMs (padIndex),
                              juce::dontSendNotification);
        sustainSlider.setValue (processor.getPadAmpSustain (padIndex),
                                juce::dontSendNotification);
        releaseSlider.setValue (processor.getPadAmpReleaseMs (padIndex),
                                juce::dontSendNotification);
        reverseButton.setToggleState (processor.isPadReversed (padIndex),
                                      juce::dontSendNotification);
        loopButton.setToggleState (processor.isPadLoopEnabled (padIndex),
                                   juce::dontSendNotification);
        snapButton.setToggleState (processor.isSampleMarkerSnapEnabled(),
                                   juce::dontSendNotification);

        titleLabel.setText ("PAD " + juce::String (padIndex + 1)
                                + "  —  " + processor.getPadDisplayName (padIndex),
                            juce::dontSendNotification);

        auto path = processor.getPadSamplePath (padIndex);
        pathLabel.setText (path.isNotEmpty() ? path : "No sample loaded",
                           juce::dontSendNotification);
        clearButton.setEnabled (path.isNotEmpty());
        loopButton.setEnabled (path.isNotEmpty());
        waveformEditor.repaint();
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (panelColour);
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 5.0f);
        g.setColour (lineColour);
        g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 5.0f, 1.0f);

        auto strip = getLocalBounds().toFloat().removeFromLeft (5.0f).reduced (1.0f);
        g.setColour (getPadColour (padIndex));
        g.fillRoundedRectangle (strip, 2.0f);

        auto footer = getLocalBounds().reduced (14).removeFromBottom (24);
        g.setColour (mutedTextColour);
        g.setFont (10.5f);
        g.drawText ("Drag markers or wheel their sample boxes. SNAP uses zero crossings; double-click resets.",
                    footer, juce::Justification::centredLeft, true);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (16);
        auto titleRow = area.removeFromTop (26);
        const int titleWidth = juce::jlimit (
            230, 360, static_cast<int> (titleRow.getWidth() * 0.45f));
        titleLabel.setBounds (titleRow.removeFromLeft (titleWidth));
        titleRow.removeFromLeft (8);
        clearButton.setBounds (titleRow.removeFromRight (24).reduced (1));
        titleRow.removeFromRight (5);
        pathLabel.setBounds (titleRow);
        area.removeFromTop (6);
        area.removeFromBottom (28);

        auto controls = area.removeFromBottom (66);
        area.removeFromBottom (6);
        waveformEditor.setBounds (area);

        layoutKnob (chokeLabel, chokeSlider, controls.removeFromLeft (58));
        controls.removeFromLeft (4);
        layoutKnob (attackLabel, attackSlider, controls.removeFromLeft (58));
        controls.removeFromLeft (4);
        layoutKnob (decayLabel, decaySlider, controls.removeFromLeft (58));
        controls.removeFromLeft (4);
        layoutKnob (sustainLabel, sustainSlider, controls.removeFromLeft (58));
        controls.removeFromLeft (4);
        layoutKnob (releaseLabel, releaseSlider, controls.removeFromLeft (58));
        controls.removeFromLeft (8);

        reverseButton.setBounds (controls.removeFromLeft (92).withSizeKeepingCentre (88, 24));
        controls.removeFromLeft (6);
        loopButton.setBounds (controls.removeFromLeft (72).withSizeKeepingCentre (68, 24));
        controls.removeFromLeft (6);
        snapButton.setBounds (controls.removeFromLeft (72).withSizeKeepingCentre (68, 24));
    }

private:
    void refreshPatternResetValues()
    {
        const auto revision = processor.getPatternChangeRevision();

        if (revision != resetValuesRevision)
        {
            for (int index = 0;
                 index < SVDrummerAudioProcessor::numberOfPads;
                 ++index)
            {
                resetChokeGroups[static_cast<std::size_t> (index)]
                    = processor.getPadChokeGroup (index);
                resetAttacks[static_cast<std::size_t> (index)]
                    = processor.getPadAmpAttackMs (index);
                resetDecays[static_cast<std::size_t> (index)]
                    = processor.getPadAmpDecayMs (index);
                resetSustains[static_cast<std::size_t> (index)]
                    = processor.getPadAmpSustain (index);
                resetReleases[static_cast<std::size_t> (index)]
                    = processor.getPadAmpReleaseMs (index);
            }

            resetValuesRevision = revision;
        }

        chokeSlider.setDoubleClickReturnValue (
            true, resetChokeGroups[static_cast<std::size_t> (padIndex)]);
        attackSlider.setDoubleClickReturnValue (
            true, resetAttacks[static_cast<std::size_t> (padIndex)]);
        decaySlider.setDoubleClickReturnValue (
            true, resetDecays[static_cast<std::size_t> (padIndex)]);
        sustainSlider.setDoubleClickReturnValue (
            true, resetSustains[static_cast<std::size_t> (padIndex)]);
        releaseSlider.setDoubleClickReturnValue (
            true, resetReleases[static_cast<std::size_t> (padIndex)]);
    }

    void configureSlider (juce::Slider& slider,
                          juce::Label& label,
                          const juce::String& labelText,
                          double minimum,
                          double maximum,
        double interval)
    {
        slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, true, 52, 14);
        slider.setRange (minimum, maximum, interval);
        slider.setMouseDragSensitivity (80);
        slider.setColour (juce::Slider::rotarySliderFillColourId, juce::Colour (0xff5fa3d1));
        slider.setColour (juce::Slider::textBoxTextColourId,
                          mutedTextColour.brighter (0.18f));
        slider.setColour (juce::Slider::textBoxBackgroundColourId,
                          juce::Colours::transparentBlack);
        slider.setColour (juce::Slider::textBoxOutlineColourId,
                          juce::Colours::transparentBlack);

        label.setText (labelText, juce::dontSendNotification);
        label.setJustificationType (juce::Justification::centredTop);
        label.setColour (juce::Label::textColourId, textColour);
        label.setFont (juce::FontOptions (9.5f, juce::Font::bold));
        addAndMakeVisible (slider);
        addAndMakeVisible (label);
    }

    static void layoutKnob (juce::Label& label,
                            juce::Slider& slider,
                            juce::Rectangle<int> bounds)
    {
        label.setBounds (bounds.removeFromTop (13));
        slider.setBounds (bounds);
    }

    SVDrummerAudioProcessor& processor;
    SVDrummerWaveformEditor waveformEditor;
    std::array<double, SVDrummerAudioProcessor::numberOfPads> resetChokeGroups {};
    std::array<double, SVDrummerAudioProcessor::numberOfPads> resetAttacks {};
    std::array<double, SVDrummerAudioProcessor::numberOfPads> resetDecays {};
    std::array<double, SVDrummerAudioProcessor::numberOfPads> resetSustains {};
    std::array<double, SVDrummerAudioProcessor::numberOfPads> resetReleases {};
    std::uint64_t resetValuesRevision =
        (std::numeric_limits<std::uint64_t>::max)();
    int padIndex = 0;
    bool updating = false;
    juce::Label titleLabel;
    juce::Label pathLabel;
    juce::Label chokeLabel;
    juce::Label attackLabel;
    juce::Label decayLabel;
    juce::Label sustainLabel;
    juce::Label releaseLabel;
    juce::Slider chokeSlider;
    SVDrummerEnvelopeSlider attackSlider;
    SVDrummerEnvelopeSlider decaySlider;
    SVDrummerEnvelopeSlider sustainSlider;
    SVDrummerEnvelopeSlider releaseSlider;
    juce::TextButton reverseButton { "REVERSE" };
    juce::TextButton loopButton { "LOOP" };
    juce::TextButton snapButton { "SNAP" };
    juce::TextButton clearButton { "X" };
};

class SVDrummerTransportButton final : public juce::Button
{
public:
    SVDrummerTransportButton() : juce::Button ("Sequencer transport")
    {
        setClickingTogglesState (true);
    }

    void paintButton (juce::Graphics& g,
                      bool highlighted,
                      bool down) override
    {
        const float diameter = static_cast<float> (
            juce::jmin (getWidth(), getHeight())) - 1.0f;
        auto bounds = juce::Rectangle<float> (diameter, diameter)
                          .withCentre (getLocalBounds().toFloat().getCentre());
        auto background = getToggleState() ? juce::Colour (0xff3e536a)
                                            : raisedPanelColour;

        if (down)
            background = background.brighter (0.18f);
        else if (highlighted)
            background = background.brighter (0.09f);

        g.setColour (background);
        g.fillEllipse (bounds);
        g.setColour (lineColour.brighter (highlighted ? 0.22f : 0.0f));
        g.drawEllipse (bounds.reduced (0.5f), 1.2f);

        const auto icon = bounds.reduced (bounds.getWidth() * 0.31f);
        g.setColour (textColour);

        if (getToggleState())
        {
            const float side = juce::jmin (icon.getWidth(), icon.getHeight());
            g.fillRoundedRectangle (
                juce::Rectangle<float> (side, side).withCentre (icon.getCentre()), 1.0f);
        }
        else
        {
            const auto centre = icon.getCentre();
            const float iconSize = juce::jmin (icon.getWidth(), icon.getHeight());
            juce::Path triangle;
            triangle.addTriangle (
                centre.x - iconSize / 3.0f, centre.y - iconSize * 0.5f,
                centre.x - iconSize / 3.0f, centre.y + iconSize * 0.5f,
                centre.x + iconSize * 2.0f / 3.0f, centre.y);
            g.fillPath (triangle);
        }
    }
};

class SVDrummerPatternSlot final : public juce::Component,
                                   public juce::FileDragAndDropTarget,
                                   public juce::DragAndDropTarget
{
public:
    SVDrummerPatternSlot (SVDrummerAudioProcessor& owner,
                          int slotIndex,
                          std::function<void()> libraryChanged,
                          std::function<void()> loadCompletedCallback)
        : processor (owner),
          patternIndex (slotIndex),
          onPatternLibraryChanged (std::move (libraryChanged)),
          onLoadCompleted (std::move (loadCompletedCallback))
    {
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    void paint (juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat().reduced (0.5f);
        const bool selected = processor.getCurrentPatternIndex() == patternIndex;
        const bool hasSteps = processor.patternHasSteps (patternIndex);
        const int midiNote = processor.getPatternMidiNote (patternIndex);
        const bool hasMidiAssignment = midiNote >= 0;
        auto background = hasMidiAssignment
                            ? (selected ? juce::Colour (0xff405a73)
                                        : raisedPanelColour)
                            : panelColour.darker (0.12f);

        if (dropHighlight)
            background = background.brighter (0.24f);

        g.setColour (background);
        g.fillRoundedRectangle (bounds, 2.5f);
        g.setColour (selected ? juce::Colour (0xff72b8e5) : lineColour);
        g.drawRoundedRectangle (bounds, 2.5f, selected ? 1.6f : 1.0f);

        auto textArea = getLocalBounds().reduced (2, 1);
        auto numberArea = textArea.removeFromTop (textArea.getHeight() * 56 / 100);
        g.setColour ((hasMidiAssignment || hasSteps)
                         ? textColour
                         : mutedTextColour.withAlpha (0.55f));
        g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
        g.drawText (juce::String (patternIndex + 1),
                    numberArea, juce::Justification::centred);

        g.setColour (midiNote >= 0 ? textColour.withAlpha (0.90f)
                                   : mutedTextColour.withAlpha (0.88f));
        g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        g.drawFittedText (
            midiNote >= 0
                ? juce::MidiMessage::getMidiNoteName (midiNote, true, true, 4)
                : juce::String ("OFF"),
            textArea, juce::Justification::centred, 1);

        if (hasSteps)
        {
            constexpr float dotSize = 5.5f;
            g.setColour (juce::Colour (0xffdc7d83));
            g.fillEllipse (bounds.getRight() - dotSize - 2.5f,
                           bounds.getY() + 2.5f, dotSize, dotSize);
        }
    }

    void mouseDown (const juce::MouseEvent& event) override
    {
        if (event.mods.isRightButtonDown())
        {
            showMenu();
            return;
        }

        processor.selectPattern (patternIndex);
        repaint();
    }

    void mouseWheelMove (const juce::MouseEvent& event,
                         const juce::MouseWheelDetails& wheel) override
    {
        if (wheel.deltaY == 0.0f)
        {
            juce::Component::mouseWheelMove (event, wheel);
            return;
        }

        int note = processor.getPatternMidiNote (patternIndex);

        if (note < 0)
            note = 60 + patternIndex;
        else
            note += wheel.deltaY > 0.0f ? 1 : -1;

        processor.setPatternMidiNote (patternIndex, juce::jlimit (0, 127, note));
        repaint();
    }

    bool isInterestedInFileDrag (const juce::StringArray& files) override
    {
        return files.size() > 0
            && (SVDrummerAudioProcessor::isSupportedPatternFile (
                    juce::File (files[0]))
                || SVDrummerAudioProcessor::isSupportedPatternSetFile (
                    juce::File (files[0]))
                || SVDrummerAudioProcessor::isSupportedProjectFile (
                    juce::File (files[0])));
    }

    void fileDragEnter (const juce::StringArray&, int, int) override
    {
        dropHighlight = true;
        repaint();
    }

    void fileDragExit (const juce::StringArray&) override
    {
        dropHighlight = false;
        repaint();
    }

    void filesDropped (const juce::StringArray& files, int, int) override
    {
        dropHighlight = false;

        if (files.size() > 0)
            loadPattern (juce::File (files[0]));
    }

    bool isInterestedInDragSource (const SourceDetails& details) override
    {
        const juce::File file (details.description.toString());
        return SVDrummerAudioProcessor::isSupportedPatternFile (file)
            || SVDrummerAudioProcessor::isSupportedPatternSetFile (file)
            || SVDrummerAudioProcessor::isSupportedProjectFile (file);
    }

    void itemDragEnter (const SourceDetails&) override
    {
        dropHighlight = true;
        repaint();
    }

    void itemDragExit (const SourceDetails&) override
    {
        dropHighlight = false;
        repaint();
    }

    void itemDropped (const SourceDetails& details) override
    {
        dropHighlight = false;
        loadPattern (juce::File (details.description.toString()));
    }

private:
    void showMenu()
    {
        juce::PopupMenu menu;
        menu.addItem (1, "Save Pattern",
                      processor.isPatternAssigned (patternIndex), false);
        menu.addSeparator();
        menu.addItem (4, "Copy",
                      processor.isPatternAssigned (patternIndex));
        menu.addItem (5, "Paste",
                      processor.canPastePatternSlot());
        menu.addItem (8, "Random");
        menu.addItem (6, "Clear",
                      processor.patternHasSteps (patternIndex));
        menu.addItem (7, "Undo Last Operation",
                      processor.canUndoPatternOperation (patternIndex));
        menu.addSeparator();
        menu.addItem (2, "MIDI Note Off", true,
                      processor.getPatternMidiNote (patternIndex) < 0);
        menu.addItem (3, "Use Default MIDI Note");

        juce::Component::SafePointer<SVDrummerPatternSlot> safeThis (this);
        menu.showMenuAsync (
            juce::PopupMenu::Options().withTargetComponent (this),
            [safeThis] (int result)
            {
                if (safeThis == nullptr || result == 0)
                    return;

                if (result == 1)
                    safeThis->saveToLibrary();
                else if (result == 4)
                    safeThis->processor.copyPatternSlot (
                        safeThis->patternIndex);
                else if (result == 5)
                    safeThis->pastePatternWithConfirmation();
                else if (result == 8)
                    safeThis->processor.randomisePatternSlot (
                        safeThis->patternIndex);
                else if (result == 6)
                    safeThis->clearPatternWithConfirmation();
                else if (result == 7)
                    safeThis->processor.undoPatternOperation (
                        safeThis->patternIndex);
                else if (result == 2)
                    safeThis->processor.setPatternMidiNote (
                        safeThis->patternIndex, -1);
                else if (result == 3)
                    safeThis->processor.setPatternMidiNote (
                        safeThis->patternIndex, 60 + safeThis->patternIndex);

                safeThis->repaint();
            });
    }

    void saveToLibrary()
    {
        auto patternsDirectory = processor.getPortablePatternsDirectory()
                                          .getChildFile ("Patterns");
        patternsDirectory.createDirectory();
        const auto initialFile = patternsDirectory.getChildFile (
            "Pattern " + juce::String (patternIndex + 1).paddedLeft ('0', 2)
                + ".svpattern");

        patternSaveChooser = std::make_unique<juce::FileChooser> (
            "Save SV-Drummer Pattern", initialFile, "*.svpattern", true);

        juce::Component::SafePointer<SVDrummerPatternSlot> safeThis (this);
        patternSaveChooser->launchAsync (
            juce::FileBrowserComponent::saveMode
                | juce::FileBrowserComponent::canSelectFiles
                | juce::FileBrowserComponent::warnAboutOverwriting,
            [safeThis] (const juce::FileChooser& chooser)
            {
                if (safeThis == nullptr)
                    return;

                auto resultFile = chooser.getResult();

                if (resultFile == juce::File())
                    return;

                if (! resultFile.hasFileExtension ("svpattern"))
                    resultFile = resultFile.withFileExtension ("svpattern");

                const auto result = safeThis->processor.savePatternSlotToFile (
                    safeThis->patternIndex, resultFile);

                if (result.failed())
                {
                    juce::AlertWindow::showMessageBoxAsync (
                        juce::MessageBoxIconType::WarningIcon,
                        "SV-Drummer Pattern Not Saved",
                        result.getErrorMessage());
                }
                else if (safeThis->onPatternLibraryChanged)
                {
                    safeThis->onPatternLibraryChanged();
                }
            });
    }

    void loadPattern (const juce::File& file)
    {
        if (SVDrummerAudioProcessor::isSupportedProjectFile (file))
        {
            loadProjectWithConfirmation (file);
            return;
        }

        if (SVDrummerAudioProcessor::isSupportedPatternSetFile (file))
        {
            loadPatternSetWithConfirmation (file);
            return;
        }

        if (! processor.patternHasSteps (patternIndex))
        {
            finishLoadingPattern (file);
            return;
        }

        juce::Component::SafePointer<SVDrummerPatternSlot> safeThis (this);
        juce::AlertWindow::showOkCancelBox (
            juce::MessageBoxIconType::QuestionIcon,
            "Replace Pattern?",
            "Pattern " + juce::String (patternIndex + 1)
                + " already contains steps. Replace it?\n\n"
                + file.getFileNameWithoutExtension(),
            "Replace", "Cancel", this,
            juce::ModalCallbackFunction::create (
                [safeThis, file] (int result)
                {
                    if (safeThis != nullptr && result != 0)
                        safeThis->finishLoadingPattern (file);
                }));
    }

    void finishLoadingPattern (const juce::File& file)
    {
        const auto result = processor.loadPatternIntoSlot (patternIndex, file);

        if (result.failed())
        {
            juce::AlertWindow::showMessageBoxAsync (
                juce::MessageBoxIconType::WarningIcon,
                "SV-Drummer",
                "Could not load the Pattern:\n\n" + result.getErrorMessage());
        }

        repaint();
    }

    void loadProjectWithConfirmation (const juce::File& file)
    {
        if (! processor.kitHasSamples() && ! processor.patternSetHasSteps())
        {
            finishLoadingProject (file);
            return;
        }

        juce::Component::SafePointer<SVDrummerPatternSlot> safeThis (this);
        juce::AlertWindow::showOkCancelBox (
            juce::MessageBoxIconType::QuestionIcon,
            "Replace Current Project?",
            "Loading this Project will replace the complete Kit and Pattern Set.\n\n"
                + file.getFileNameWithoutExtension(),
            "Load Project", "Cancel", this,
            juce::ModalCallbackFunction::create (
                [safeThis, file] (int result)
                {
                    if (safeThis != nullptr && result != 0)
                        safeThis->finishLoadingProject (file);
                }));
    }

    void finishLoadingProject (const juce::File& file)
    {
        const auto result = processor.loadProjectFromFile (file);

        if (result.failed())
        {
            juce::AlertWindow::showMessageBoxAsync (
                juce::MessageBoxIconType::WarningIcon,
                "SV-Drummer",
                "Could not load the Project:\n\n" + result.getErrorMessage());
        }
        else if (onLoadCompleted)
        {
            onLoadCompleted();
        }

        repaint();
    }

    void pastePatternWithConfirmation()
    {
        if (! processor.patternHasSteps (patternIndex))
        {
            processor.pastePatternSlot (patternIndex);
            repaint();
            return;
        }

        juce::Component::SafePointer<SVDrummerPatternSlot> safeThis (this);
        juce::AlertWindow::showOkCancelBox (
            juce::MessageBoxIconType::QuestionIcon,
            "Replace Pattern?",
            "Pattern " + juce::String (patternIndex + 1)
                + " already contains steps. Replace it?",
            "Replace", "Cancel", this,
            juce::ModalCallbackFunction::create (
                [safeThis] (int result)
                {
                    if (safeThis != nullptr && result != 0)
                    {
                        safeThis->processor.pastePatternSlot (
                            safeThis->patternIndex);
                        safeThis->repaint();
                    }
                }));
    }

    void clearPatternWithConfirmation()
    {
        juce::Component::SafePointer<SVDrummerPatternSlot> safeThis (this);
        juce::AlertWindow::showOkCancelBox (
            juce::MessageBoxIconType::QuestionIcon,
            "Clear Pattern?",
            "Clear all sixteen Sequences from Pattern "
                + juce::String (patternIndex + 1)
                + "?\n\nLane timing and the Pattern MIDI note will remain unchanged.",
            "Clear", "Cancel", this,
            juce::ModalCallbackFunction::create (
                [safeThis] (int result)
                {
                    if (safeThis != nullptr && result != 0)
                    {
                        safeThis->processor.clearPatternSlot (
                            safeThis->patternIndex);
                        safeThis->repaint();
                    }
                }));
    }

    void loadPatternSetWithConfirmation (const juce::File& file)
    {
        if (! processor.patternSetHasSteps())
        {
            finishLoadingPatternSet (file);
            return;
        }

        juce::Component::SafePointer<SVDrummerPatternSlot> safeThis (this);
        juce::AlertWindow::showOkCancelBox (
            juce::MessageBoxIconType::QuestionIcon,
            "Replace Pattern Set?",
            "Loading this Pattern Set will replace all sixteen Patterns.\n\n"
                + file.getFileNameWithoutExtension(),
            "Load Pattern Set", "Cancel", this,
            juce::ModalCallbackFunction::create (
                [safeThis, file] (int result)
                {
                    if (safeThis != nullptr && result != 0)
                        safeThis->finishLoadingPatternSet (file);
                }));
    }

    void finishLoadingPatternSet (const juce::File& file)
    {
        const auto result = processor.loadPatternSetFromFile (file);

        if (result.failed())
        {
            juce::AlertWindow::showMessageBoxAsync (
                juce::MessageBoxIconType::WarningIcon,
                "SV-Drummer",
                "Could not load the Pattern Set:\n\n" + result.getErrorMessage());
        }
        repaint();
    }

    SVDrummerAudioProcessor& processor;
    int patternIndex = 0;
    std::function<void()> onPatternLibraryChanged;
    std::function<void()> onLoadCompleted;
    std::unique_ptr<juce::FileChooser> patternSaveChooser;
    bool dropHighlight = false;
};

class SVDrummerSequencerPanel final : public juce::Component
{
public:
    SVDrummerSequencerPanel (SVDrummerAudioProcessor& owner,
                             std::function<void()> patternLibraryChanged,
                             std::function<void (int)> laneSelectionChanged,
                             std::function<void()> loadCompletedCallback)
        : processor (owner),
          onPatternLibraryChanged (std::move (patternLibraryChanged)),
          onLaneSelectionChanged (std::move (laneSelectionChanged)),
          onLoadCompleted (std::move (loadCompletedCallback)),
          barScroll (false)
    {
        enableButton.onClick = [this]
        {
            const auto midiMode = processor.getPatternMidiMode();

            if (midiMode != SVDrummerAudioProcessor::PatternMidiMode::select)
            {
                if (processor.isSequencerEnabled())
                    processor.stopPatternMidiPlayback();

                syncFromProcessor();
                return;
            }

            processor.setSequencerEnabled (enableButton.getToggleState());
            syncFromProcessor();
        };

        configureLabel (midiModeLabel, "MIDI MODE");
        midiModeButton.setClickingTogglesState (false);
        midiModeButton.setColour (juce::TextButton::buttonColourId,
                                  raisedPanelColour);
        midiModeButton.setColour (juce::TextButton::buttonOnColourId,
                                  juce::Colour (0xff3e536a));
        midiModeButton.onClick = [this]
        {
            const int nextMode =
                (static_cast<int> (processor.getPatternMidiMode()) + 1) % 3;
            processor.setPatternMidiMode (
                static_cast<SVDrummerAudioProcessor::PatternMidiMode> (nextMode));
            syncFromProcessor();
        };

        configureSequencerKnob (lengthSlider, 1.0,
                                static_cast<double> (SVDrummerAudioProcessor::maximumPatternBars));
        lengthSlider.textFromValueFunction = [] (double value)
        {
            const int bars = juce::roundToInt (value);
            return juce::String (bars) + (bars == 1 ? " Bar" : " Bars");
        };
        lengthSlider.onValueChange = [this]
        {
            if (updatingControls)
                return;

            processor.setPatternBars (juce::roundToInt (lengthSlider.getValue()));
            updateScrollRange();
            syncLaneControls();
            repaint();
        };

        configureSequencerKnob (viewSlider, 0.0, 2.0);
        viewSlider.setDoubleClickReturnValue (true, 0.0);
        viewSlider.textFromValueFunction = [] (double value)
        {
            const int index = juce::jlimit (0, 2, juce::roundToInt (value));
            const int bars = index == 0 ? 1 : index == 1 ? 2 : 4;
            return juce::String (bars) + (bars == 1 ? " Bar" : " Bars");
        };
        viewSlider.onValueChange = [this]
        {
            if (! updatingControls)
            {
                const int index = juce::jlimit (
                    0, 2, juce::roundToInt (viewSlider.getValue()));
                visibleBars = index == 0 ? 1 : index == 1 ? 2 : 4;
                updateScrollRange();
                repaint();
            }
        };

        configureSequencerKnob (
            divisionSlider, 0.0,
            static_cast<double> (SVDrummerAudioProcessor::sequencerDivisionCount - 1));
        divisionSlider.textFromValueFunction = [] (double value)
        {
            return SVDrummerAudioProcessor::getSequencerDivisionName (
                juce::roundToInt (value));
        };
        divisionSlider.onValueChange = [this]
        {
            if (updatingControls)
                return;

            processor.setLaneDivision (
                selectedLane, juce::roundToInt (divisionSlider.getValue()));
            syncLaneControls();
            repaint();
        };

        configureSequencerKnob (loopLengthSlider, 1.0, 16.0);
        loopLengthSlider.textFromValueFunction = [] (double value)
        {
            const int steps = juce::roundToInt (value);
            return juce::String (steps) + (steps == 1 ? " Step" : " Steps");
        };
        loopLengthSlider.onValueChange = [this]
        {
            if (! updatingControls)
            {
                processor.setLaneLoopLength (
                    selectedLane, juce::roundToInt (loopLengthSlider.getValue()));
                repaint();
            }
        };

        configureSequencerKnob (padVolumeSlider, -60.0, 6.0, 0.1);
        padVolumeSlider.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        padVolumeSlider.setTextValueSuffix (" dB");
        padVolumeSlider.onValueChange = [this]
        {
            if (! updatingControls)
                processor.setPadVolumeDb (
                    selectedLane, static_cast<float> (padVolumeSlider.getValue()));
            repaint (padVolumeValueBounds);
        };

        configureSequencerKnob (padPanSlider, -1.0, 1.0, 0.01);
        padPanSlider.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        padPanSlider.textFromValueFunction = [] (double value)
        {
            if (std::abs (value) < 0.005)
                return juce::String ("C");

            return value < 0.0 ? "L " + juce::String (std::abs (value), 2)
                               : "R " + juce::String (value, 2);
        };
        padPanSlider.onValueChange = [this]
        {
            if (! updatingControls)
                processor.setPadPan (
                    selectedLane, static_cast<float> (padPanSlider.getValue()));
            repaint (padPanValueBounds);
        };

        configureSequencerKnob (padTuneSlider, -24.0, 24.0, 0.01);
        padTuneSlider.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
        padTuneSlider.setTextValueSuffix (" st");
        padTuneSlider.onValueChange = [this]
        {
            if (! updatingControls)
                processor.setPadTuneSemitones (
                    selectedLane, static_cast<float> (padTuneSlider.getValue()));
            repaint (padTuneValueBounds);
        };

        configureLabel (lengthLabel, "LENGTH");
        configureLabel (viewLabel, "VIEW");
        configureLabel (divisionLabel, "DIV");
        configureLabel (loopLabel, "LOOP");
        configureLabel (padVolumeLabel, "PAD VOL");
        configureLabel (padPanLabel, "PAD PAN");
        configureLabel (padTuneLabel, "PAD TUNE");

        patternMenuButton.onClick = [this] { showPatternMenu(); };

        laneButton.setInterceptsMouseClicks (false, false);
        laneButton.setColour (juce::TextButton::buttonColourId,
                              getPadColour (selectedLane).withAlpha (0.58f));

        barScroll.setAutoHide (false);
        barScroll.setSingleStepSize (1.0);
        barScroll.setColour (juce::ScrollBar::backgroundColourId, panelColour);
        barScroll.setColour (juce::ScrollBar::thumbColourId,
                             juce::Colour (0xff3c9fc1));
        barScroll.setColour (juce::ScrollBar::trackColourId,
                             raisedPanelColour.darker (0.28f));

        addAndMakeVisible (enableButton);
        addAndMakeVisible (midiModeLabel);
        addAndMakeVisible (midiModeButton);
        addAndMakeVisible (lengthLabel);
        addAndMakeVisible (lengthSlider);
        addAndMakeVisible (viewLabel);
        addAndMakeVisible (viewSlider);
        addAndMakeVisible (laneButton);
        addAndMakeVisible (divisionLabel);
        addAndMakeVisible (divisionSlider);
        addAndMakeVisible (loopLabel);
        addAndMakeVisible (loopLengthSlider);
        addAndMakeVisible (padVolumeLabel);
        addAndMakeVisible (padVolumeSlider);
        addAndMakeVisible (padPanLabel);
        addAndMakeVisible (padPanSlider);
        addAndMakeVisible (padTuneLabel);
        addAndMakeVisible (padTuneSlider);
        addAndMakeVisible (barScroll);
        addAndMakeVisible (patternMenuButton);

        for (int patternIndex = 0;
             patternIndex < SVDrummerAudioProcessor::numberOfPatterns;
             ++patternIndex)
        {
            patternSlots[static_cast<std::size_t> (patternIndex)]
                = std::make_unique<SVDrummerPatternSlot> (
                    processor, patternIndex, onPatternLibraryChanged,
                    onLoadCompleted);
            addAndMakeVisible (*patternSlots[static_cast<std::size_t> (patternIndex)]);
        }

        updatingControls = true;
        viewSlider.setValue (0.0, juce::dontSendNotification);
        viewSlider.updateText();
        updatingControls = false;
        syncFromProcessor();
    }

    void refresh()
    {
        syncFromProcessor();

        for (auto& slot : patternSlots)
            slot->repaint();

        repaint();
    }

    int getSelectedLane() const noexcept
    {
        return selectedLane;
    }

    void setSelectedLane (int lane)
    {
        selectLane (lane);
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (panelColour);
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 5.0f);
        g.setColour (lineColour);
        g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 5.0f, 1.0f);

        auto area = getLocalBounds().reduced (10);

        if (! rulerBounds.isEmpty())
        {
            auto rulerSteps = rulerBounds;
            auto rulerLabel = rulerSteps.removeFromLeft (laneLabelWidth);
            rulerSteps.removeFromLeft (4);
            const int startBar = getStartBar();
            const int barsShown = getBarsShown();
            const float barWidth = static_cast<float> (rulerSteps.getWidth())
                                 / static_cast<float> (juce::jmax (1, barsShown));

            g.setColour (lineColour.brighter (0.08f));
            g.drawHorizontalLine (rulerBounds.getY(),
                                  0.5f, static_cast<float> (getWidth()) - 0.5f);
            g.drawHorizontalLine (rulerBounds.getBottom() - 1,
                                  0.5f, static_cast<float> (getWidth()) - 0.5f);

            g.setColour (mutedTextColour.brighter (0.12f));
            g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
            g.drawText ("LANE", rulerLabel.reduced (4, 0),
                        juce::Justification::centred);

            for (int bar = 0; bar < barsShown; ++bar)
            {
                auto barBounds = juce::Rectangle<float> (
                    static_cast<float> (rulerSteps.getX()) + barWidth * static_cast<float> (bar),
                    static_cast<float> (rulerSteps.getY()), barWidth,
                    static_cast<float> (rulerSteps.getHeight()));
                g.drawText ("BAR " + juce::String (startBar + bar + 1),
                            barBounds.toNearestInt(), juce::Justification::centred);
            }
        }

        drawBeatGrid (g);
        drawSequenceRows (g);
        drawPlayhead (g);
        drawPadKnobValues (g);

        auto patternArea = patternBounds;
        auto patternLabel = patternArea.removeFromLeft (laneLabelWidth);
        g.setColour (mutedTextColour);
        g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
        g.drawText ("PATTERNS", patternLabel.removeFromTop (18),
                    juce::Justification::centred);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (10);

        auto controls = area.removeFromTop (66);
        controls.translate (0, -3);
        constexpr int transportSize = 40;
        const int controlY = controls.getY()
                           + (controls.getHeight() - transportSize) / 2;
        enableButton.setBounds (
            controls.removeFromLeft (44).withY (controlY).withHeight (transportSize)
                    .withSizeKeepingCentre (transportSize, transportSize));
        controls.removeFromLeft (8);

        auto midiModeGroup = controls.removeFromLeft (74);
        midiModeLabel.setBounds (midiModeGroup.removeFromTop (13));
        midiModeButton.setBounds (
            midiModeGroup.withY (controlY + 6).withHeight (27).reduced (3, 0));
        controls.removeFromLeft (6);

        auto placeKnob = [&controls] (juce::Label& label, juce::Slider& slider)
        {
            auto group = controls.removeFromLeft (66);
            label.setBounds (group.removeFromTop (13));
            slider.setBounds (group);
            controls.removeFromLeft (4);
        };

        placeKnob (lengthLabel, lengthSlider);
        placeKnob (viewLabel, viewSlider);
        laneButton.setBounds (controls.removeFromLeft (72)
                                      .withY (controlY + 6).withHeight (27));
        controls.removeFromLeft (8);
        placeKnob (divisionLabel, divisionSlider);
        placeKnob (loopLabel, loopLengthSlider);
        controls.removeFromLeft (8);

        auto placePadKnob = [&controls] (juce::Label& label,
                                         juce::Slider& slider,
                                         juce::Rectangle<int>& valueBounds)
        {
            auto group = controls.removeFromLeft (66);
            label.setBounds (group.removeFromTop (13));
            valueBounds = group.removeFromBottom (14);
            slider.setBounds (group);
            controls.removeFromLeft (4);
        };

        placePadKnob (padVolumeLabel, padVolumeSlider, padVolumeValueBounds);
        placePadKnob (padPanLabel, padPanSlider, padPanValueBounds);
        placePadKnob (padTuneLabel, padTuneSlider, padTuneValueBounds);

        area.removeFromTop (1);
        patternBounds = area.removeFromBottom (46);
        area.removeFromBottom (4);
        auto scrollRow = area.removeFromBottom (8);
        scrollRow.removeFromLeft (laneLabelWidth + 4);
        barScroll.setBounds (scrollRow);
        area.removeFromBottom (3);
        rulerBounds = area.removeFromTop (17);
        sequenceRowsBounds = area;

        auto patternLayout = patternBounds;
        auto patternLabelArea = patternLayout.removeFromLeft (laneLabelWidth);
        patternLabelArea.removeFromTop (19);
        patternMenuButton.setBounds (
            patternLabelArea.removeFromTop (23).reduced (8, 1));

        auto slotsArea = patternLayout;
        slotsArea.removeFromLeft (4);
        slotsArea = slotsArea.withSizeKeepingCentre (
            slotsArea.getWidth(), juce::jmin (36, slotsArea.getHeight()));
        const float slotWidth = static_cast<float> (slotsArea.getWidth())
                              / static_cast<float> (SVDrummerAudioProcessor::numberOfPatterns);

        for (int patternIndex = 0;
             patternIndex < SVDrummerAudioProcessor::numberOfPatterns;
             ++patternIndex)
        {
            const int left = juce::roundToInt (
                static_cast<float> (slotsArea.getX())
                + slotWidth * static_cast<float> (patternIndex));
            const int right = juce::roundToInt (
                static_cast<float> (slotsArea.getX())
                + slotWidth * static_cast<float> (patternIndex + 1));
            patternSlots[static_cast<std::size_t> (patternIndex)]->setBounds (
                left, slotsArea.getY(), juce::jmax (1, right - left - 2),
                slotsArea.getHeight());
        }
    }

    void mouseDown (const juce::MouseEvent& event) override
    {
        stepGestureActive = false;
        gestureLane = -1;
        lastGestureVisibleStep = -1;

        if (! sequenceRowsBounds.contains (event.getPosition()))
            return;

        const int lane = laneAt (event.position.y);

        if (lane < 0)
            return;

        selectLane (lane);

        const int visibleStep = visibleStepAt (event.position.x, lane);

        if (visibleStep < 0)
            return;

        if (! event.mods.isLeftButtonDown() && ! event.mods.isRightButtonDown())
            return;

        gestureLane = lane;
        gestureVelocity = event.mods.isRightButtonDown()
                            ? 0
                            : lastDrawVelocity;
        lastGestureVisibleStep = visibleStep;
        stepGestureActive = true;
        applyStepGesture (gestureLane, visibleStep);
    }

    void mouseDrag (const juce::MouseEvent& event) override
    {
        if (! stepGestureActive || gestureLane < 0)
            return;

        const int visibleStep = visibleStepAt (event.position.x, gestureLane);

        if (visibleStep < 0 || visibleStep == lastGestureVisibleStep)
            return;

        const int first = juce::jmin (lastGestureVisibleStep, visibleStep);
        const int last = juce::jmax (lastGestureVisibleStep, visibleStep);

        for (int step = first; step <= last; ++step)
            applyStepGesture (gestureLane, step);

        lastGestureVisibleStep = visibleStep;
        repaint();
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        stepGestureActive = false;
        gestureLane = -1;
        lastGestureVisibleStep = -1;
    }

    void mouseWheelMove (const juce::MouseEvent& event,
                         const juce::MouseWheelDetails& wheel) override
    {
        if (wheel.deltaY == 0.0f || ! sequenceRowsBounds.contains (event.getPosition()))
        {
            juce::Component::mouseWheelMove (event, wheel);
            return;
        }

        const int lane = laneAt (event.position.y);
        const int visibleStep = visibleStepAt (event.position.x, lane);

        if (lane < 0 || visibleStep < 0)
        {
            juce::Component::mouseWheelMove (event, wheel);
            return;
        }

        const int dataStep = dataStepForVisibleStep (lane, visibleStep);
        const int currentVelocity = processor.getSequenceStepVelocity (lane, dataStep);

        if (currentVelocity <= 0)
            return;

        const int newVelocity = juce::jlimit (
            1, 127, currentVelocity + (wheel.deltaY > 0.0f ? 5 : -5));
        processor.setSequenceStepVelocity (lane, dataStep, newVelocity);
        lastDrawVelocity = newVelocity;
        selectLane (lane);
        repaint();
    }

private:
    void showPatternMenu()
    {
        juce::PopupMenu menu;
        menu.addItem (1, "Save Pattern Set");

        juce::Component::SafePointer<SVDrummerSequencerPanel> safeThis (this);
        menu.showMenuAsync (
            juce::PopupMenu::Options().withTargetComponent (&patternMenuButton),
            [safeThis] (int result)
            {
                if (safeThis != nullptr && result == 1)
                    safeThis->savePatternSetToLibrary();
            });
    }

    void savePatternSetToLibrary()
    {
        auto patternSetsDirectory = processor.getPortablePatternsDirectory()
                                            .getChildFile ("Pattern Sets");
        patternSetsDirectory.createDirectory();
        const auto initialFile = patternSetsDirectory.getChildFile (
            "New Pattern Set.svpatternset");

        patternSetSaveChooser = std::make_unique<juce::FileChooser> (
            "Save SV-Drummer Pattern Set", initialFile, "*.svpatternset", true);

        juce::Component::SafePointer<SVDrummerSequencerPanel> safeThis (this);
        patternSetSaveChooser->launchAsync (
            juce::FileBrowserComponent::saveMode
                | juce::FileBrowserComponent::canSelectFiles
                | juce::FileBrowserComponent::warnAboutOverwriting,
            [safeThis] (const juce::FileChooser& chooser)
            {
                if (safeThis == nullptr)
                    return;

                auto resultFile = chooser.getResult();

                if (resultFile == juce::File())
                    return;

                if (! resultFile.hasFileExtension ("svpatternset"))
                    resultFile = resultFile.withFileExtension ("svpatternset");

                const auto result = safeThis->processor.savePatternSetToFile (
                    resultFile);

                if (result.failed())
                {
                    juce::AlertWindow::showMessageBoxAsync (
                        juce::MessageBoxIconType::WarningIcon,
                        "SV-Drummer Pattern Set Not Saved",
                        result.getErrorMessage());
                }
                else if (safeThis->onPatternLibraryChanged)
                {
                    safeThis->onPatternLibraryChanged();
                }
            });
    }

    void drawPadKnobValues (juce::Graphics& g)
    {
        g.setColour (mutedTextColour.brighter (0.18f));
        g.setFont (juce::FontOptions (11.5f, juce::Font::bold));
        g.drawText (padVolumeSlider.getTextFromValue (padVolumeSlider.getValue()),
                    padVolumeValueBounds, juce::Justification::centred, false);
        g.drawText (padPanSlider.getTextFromValue (padPanSlider.getValue()),
                    padPanValueBounds, juce::Justification::centred, false);
        g.drawText (padTuneSlider.getTextFromValue (padTuneSlider.getValue()),
                    padTuneValueBounds, juce::Justification::centred, false);
    }

    void configureSequencerKnob (juce::Slider& slider,
                                 double minimum,
                                 double maximum,
                                 double interval = 1.0)
    {
        slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, true, 64, 14);
        slider.setNumDecimalPlacesToDisplay (
            interval < 0.1 ? 2 : interval < 1.0 ? 1 : 0);
        slider.setRange (minimum, maximum, interval);
        slider.setMouseDragSensitivity (80);
        slider.setColour (juce::Slider::rotarySliderFillColourId,
                          juce::Colour (0xff5fa3d1));
        slider.setColour (juce::Slider::textBoxTextColourId,
                          mutedTextColour.brighter (0.18f));
        slider.setColour (juce::Slider::textBoxBackgroundColourId,
                          juce::Colours::transparentBlack);
        slider.setColour (juce::Slider::textBoxOutlineColourId,
                          juce::Colours::transparentBlack);
    }

    void configureLabel (juce::Label& label, const juce::String& text)
    {
        label.setText (text, juce::dontSendNotification);
        label.setJustificationType (juce::Justification::centred);
        label.setColour (juce::Label::textColourId, mutedTextColour);
        label.setFont (juce::FontOptions (9.5f, juce::Font::bold));
    }

    void syncFromProcessor()
    {
        updatingControls = true;
        refreshPatternResetValues();
        const bool enabled = processor.isSequencerEnabled();
        const auto midiMode = processor.getPatternMidiMode();
        const bool triggeredMode =
            midiMode != SVDrummerAudioProcessor::PatternMidiMode::select;
        enableButton.setToggleState (enabled, juce::dontSendNotification);

        enableButton.repaint();
        midiModeButton.setToggleState (triggeredMode, juce::dontSendNotification);
        midiModeButton.setColour (
            juce::TextButton::buttonOnColourId,
            midiMode == SVDrummerAudioProcessor::PatternMidiMode::hold
                ? juce::Colour (0xff6a4f7e)
                : juce::Colour (0xff3e536a));
        midiModeButton.setButtonText (
            midiMode == SVDrummerAudioProcessor::PatternMidiMode::gate ? "GATE"
          : midiMode == SVDrummerAudioProcessor::PatternMidiMode::hold ? "HOLD"
                                                                       : "SELECT");
        lengthSlider.setValue (processor.getPatternBars(), juce::dontSendNotification);
        lengthSlider.updateText();
        viewSlider.updateText();
        updatingControls = false;
        updateScrollRange();
        syncLaneControls();
    }

    void syncLaneControls()
    {
        updatingControls = true;
        refreshPatternResetValues();
        const int division = processor.getLaneDivision (selectedLane);
        divisionSlider.setValue (division, juce::dontSendNotification);
        divisionSlider.updateText();
        loopLengthSlider.setRange (1.0,
                                   static_cast<double> (
                                       processor.getLaneMaximumLoopLength (selectedLane)),
                                   1.0);
        loopLengthSlider.setValue (processor.getLaneLoopLength (selectedLane),
                                   juce::dontSendNotification);
        loopLengthSlider.updateText();
        padVolumeSlider.setValue (processor.getPadVolumeDb (selectedLane),
                                  juce::dontSendNotification);
        padPanSlider.setValue (processor.getPadPan (selectedLane),
                               juce::dontSendNotification);
        padTuneSlider.setValue (processor.getPadTuneSemitones (selectedLane),
                                juce::dontSendNotification);
        laneButton.setButtonText ("LANE " + juce::String (selectedLane + 1));
        const auto padAccent = getPadColour (selectedLane);
        laneButton.setColour (juce::TextButton::buttonColourId,
                              padAccent.withAlpha (0.58f));

        for (auto* slider : { &padVolumeSlider, &padPanSlider, &padTuneSlider })
            slider->setColour (juce::Slider::rotarySliderFillColourId, padAccent);

        for (auto* label : { &padVolumeLabel, &padPanLabel, &padTuneLabel })
            label->setColour (juce::Label::textColourId,
                              padAccent.brighter (0.20f));
        updatingControls = false;
    }

    void refreshPatternResetValues()
    {
        const auto revision = processor.getPatternChangeRevision();

        if (revision != resetValuesRevision)
        {
            resetLength = processor.getPatternBars();

            for (int lane = 0;
                 lane < SVDrummerAudioProcessor::numberOfPads;
                 ++lane)
            {
                resetDivisions[static_cast<std::size_t> (lane)]
                    = processor.getLaneDivision (lane);
                resetLoopLengths[static_cast<std::size_t> (lane)]
                    = processor.getLaneLoopLength (lane);
                resetPadVolumes[static_cast<std::size_t> (lane)]
                    = processor.getPadVolumeDb (lane);
                resetPadPans[static_cast<std::size_t> (lane)]
                    = processor.getPadPan (lane);
                resetPadTunes[static_cast<std::size_t> (lane)]
                    = processor.getPadTuneSemitones (lane);
            }

            resetValuesRevision = revision;
        }

        lengthSlider.setDoubleClickReturnValue (true, resetLength);
        divisionSlider.setDoubleClickReturnValue (
            true, resetDivisions[static_cast<std::size_t> (selectedLane)]);
        loopLengthSlider.setDoubleClickReturnValue (
            true, resetLoopLengths[static_cast<std::size_t> (selectedLane)]);
        padVolumeSlider.setDoubleClickReturnValue (
            true, resetPadVolumes[static_cast<std::size_t> (selectedLane)]);
        padPanSlider.setDoubleClickReturnValue (
            true, resetPadPans[static_cast<std::size_t> (selectedLane)]);
        padTuneSlider.setDoubleClickReturnValue (
            true, resetPadTunes[static_cast<std::size_t> (selectedLane)]);
    }

    void updateScrollRange()
    {
        const double totalBars = static_cast<double> (processor.getPatternBars());
        const double shown = static_cast<double> (getBarsShown());
        const double oldStart = barScroll.getCurrentRangeStart();
        const double maximumStart = juce::jmax (0.0, totalBars - shown);
        barScroll.setRangeLimits (0.0, totalBars, juce::dontSendNotification);
        barScroll.setCurrentRange (juce::jlimit (0.0, maximumStart, oldStart),
                                   shown, juce::dontSendNotification);
    }

    int getBarsShown() const
    {
        return juce::jmin (visibleBars, processor.getPatternBars());
    }

    int getStartBar() const
    {
        return juce::jlimit (0,
                             juce::jmax (0, processor.getPatternBars() - getBarsShown()),
                             juce::roundToInt (barScroll.getCurrentRangeStart()));
    }

    int laneAt (float y) const
    {
        if (sequenceRowsBounds.isEmpty())
            return -1;

        const float relativeY = y - static_cast<float> (sequenceRowsBounds.getY());
        const float rowHeight = static_cast<float> (sequenceRowsBounds.getHeight()) / 16.0f;
        return juce::jlimit (0, 15, static_cast<int> (std::floor (relativeY / rowHeight)));
    }

    void selectLane (int lane)
    {
        const int newLane = juce::jlimit (0, 15, lane);

        if (newLane != selectedLane)
        {
            selectedLane = newLane;
            syncLaneControls();

            if (onLaneSelectionChanged)
                onLaneSelectionChanged (selectedLane);

            repaint();
        }
    }

    int visibleStepAt (float x, int lane) const
    {
        if (lane < 0 || lane >= 16)
            return -1;

        auto stepsBounds = sequenceRowsBounds;
        stepsBounds.removeFromLeft (laneLabelWidth + 4);

        if (x < static_cast<float> (stepsBounds.getX())
            || x >= static_cast<float> (stepsBounds.getRight()))
            return -1;

        const int division = processor.getLaneDivision (lane);
        const int stepsPerBar = SVDrummerAudioProcessor::getSequencerStepsPerBar (division);
        const int visibleSteps = juce::jmax (1, getBarsShown() * stepsPerBar);
        const float relativeX = x - static_cast<float> (stepsBounds.getX());
        return juce::jlimit (
            0, visibleSteps - 1,
            static_cast<int> (std::floor (
                relativeX * static_cast<float> (visibleSteps)
                / static_cast<float> (juce::jmax (1, stepsBounds.getWidth())))));
    }

    int dataStepForVisibleStep (int lane, int visibleStep) const
    {
        const int division = processor.getLaneDivision (lane);
        const int stepsPerBar = SVDrummerAudioProcessor::getSequencerStepsPerBar (division);
        const int globalStep = getStartBar() * stepsPerBar + visibleStep;
        const int loopLength = juce::jmax (1, processor.getLaneLoopLength (lane));
        return globalStep % loopLength;
    }

    void applyStepGesture (int lane, int visibleStep)
    {
        const int dataStep = dataStepForVisibleStep (lane, visibleStep);
        const int currentVelocity = processor.getSequenceStepVelocity (lane, dataStep);

        if (gestureVelocity == 0 || currentVelocity <= 0)
            processor.setSequenceStepVelocity (lane, dataStep, gestureVelocity);

        repaint();
    }

    void drawSequenceRows (juce::Graphics& g)
    {
        if (sequenceRowsBounds.isEmpty())
            return;

        const float rowHeight = static_cast<float> (sequenceRowsBounds.getHeight()) / 16.0f;
        const int startBar = getStartBar();
        const int barsShown = getBarsShown();
        for (int lane = 0; lane < 16; ++lane)
        {
            const float rowY = static_cast<float> (sequenceRowsBounds.getY())
                             + rowHeight * static_cast<float> (lane);
            auto row = juce::Rectangle<float> (
                static_cast<float> (sequenceRowsBounds.getX()), rowY,
                static_cast<float> (sequenceRowsBounds.getWidth()), rowHeight);
            auto label = row.removeFromLeft (static_cast<float> (laneLabelWidth));
            row.removeFromLeft (4.0f);
            const auto accent = getPadColour (lane);

            g.setColour (lane == selectedLane ? accent.withAlpha (0.72f)
                                              : accent.withAlpha (0.38f));
            g.fillRoundedRectangle (label.reduced (0.0f, 0.6f), 1.5f);

            auto laneNumber = label.reduced (2.0f, 1.8f);
            laneNumber.setWidth (23.0f);
            auto divisionText = label;
            divisionText.setLeft (laneNumber.getRight() + 4.0f);
            g.setColour (juce::Colours::black.withAlpha (0.50f));
            g.fillRoundedRectangle (laneNumber, 1.5f);
            g.setColour (juce::Colours::white.withAlpha (0.90f));
            g.setFont (juce::FontOptions (
                juce::jmax (9.0f, juce::jmin (10.5f, rowHeight * 0.62f)),
                juce::Font::bold));
            g.drawText (juce::String (lane + 1), laneNumber.toNearestInt(),
                        juce::Justification::centred, false);
            g.drawFittedText (
                SVDrummerAudioProcessor::getSequencerDivisionName (
                    processor.getLaneDivision (lane)),
                divisionText.toNearestInt().reduced (2, 0),
                juce::Justification::centred, 1);

            const int division = processor.getLaneDivision (lane);
            const int stepsPerBar = SVDrummerAudioProcessor::getSequencerStepsPerBar (division);
            const int visibleSteps = juce::jmax (1, barsShown * stepsPerBar);
            const int loopLength = juce::jmax (1, processor.getLaneLoopLength (lane));
            const float cellWidth = row.getWidth() / static_cast<float> (visibleSteps);
            for (int visibleStep = 0; visibleStep < visibleSteps; ++visibleStep)
            {
                const int globalStep = startBar * stepsPerBar + visibleStep;
                const int dataStep = globalStep % loopLength;
                const int velocity = processor.getSequenceStepVelocity (lane, dataStep);
                const bool repeatedOccurrence = globalStep >= loopLength;
                const float repeatScale = repeatedOccurrence ? 0.34f : 1.0f;
                auto cell = juce::Rectangle<float> (
                    row.getX() + cellWidth * static_cast<float> (visibleStep),
                    row.getY(), juce::jmax (0.75f, cellWidth - 0.7f), row.getHeight());
                auto inner = cell.reduced (0.0f, 0.7f);

                const float backgroundAlpha = dataStep == 0 ? 0.20f : 0.10f;
                g.setColour (accent.withAlpha (backgroundAlpha * repeatScale));
                g.fillRoundedRectangle (inner, 1.0f);

                if (velocity > 0)
                {
                    const float amount = static_cast<float> (velocity) / 127.0f;
                    auto velocityFill = inner;
                    velocityFill.setTop (velocityFill.getBottom()
                                         - velocityFill.getHeight() * amount);
                    g.setColour (accent.withAlpha (
                        (0.48f + amount * 0.48f) * repeatScale));
                    g.fillRoundedRectangle (velocityFill, 1.0f);
                }

                if (dataStep == 0 && globalStep > 0
                    && visibleStep % stepsPerBar != 0)
                {
                    g.setColour (accent.withAlpha (repeatedOccurrence ? 0.26f : 0.50f));
                    g.drawVerticalLine (juce::roundToInt (cell.getX()),
                                        row.getY(), row.getBottom());
                }
            }
        }
    }

    void drawBeatGrid (juce::Graphics& g)
    {
        if (sequenceRowsBounds.isEmpty())
            return;

        auto stepArea = sequenceRowsBounds.toFloat();
        stepArea.removeFromLeft (static_cast<float> (laneLabelWidth + 4));
        const int totalBeats = juce::jmax (1, getBarsShown() * 4);

        for (int beat = 0; beat <= totalBeats; ++beat)
        {
            const bool barLine = beat % 4 == 0;
            const float x = stepArea.getX()
                          + stepArea.getWidth() * static_cast<float> (beat)
                                                   / static_cast<float> (totalBeats);
            g.setColour (barLine ? textColour.withAlpha (0.46f)
                                 : lineColour.brighter (0.32f));
            g.drawLine (x, stepArea.getY(), x, stepArea.getBottom(),
                        barLine ? 1.8f : 1.0f);
        }
    }

    void drawPlayhead (juce::Graphics& g)
    {
        if (! processor.isSequencerEnabled()
            || processor.getActiveSequenceStep (0) < 0
            || sequenceRowsBounds.isEmpty())
            return;

        auto stepArea = sequenceRowsBounds;
        stepArea.removeFromLeft (laneLabelWidth + 4);
        const double visibleStart = static_cast<double> (getStartBar()) * 4.0;
        const double visibleLength = static_cast<double> (getBarsShown()) * 4.0;
        const double position = processor.getSequencerPatternPositionQuarterNotes();

        if (position < visibleStart || position >= visibleStart + visibleLength)
            return;

        const int sixteenthColumns = juce::jmax (1, getBarsShown() * 16);
        const int column = juce::jlimit (
            0, sixteenthColumns - 1,
            static_cast<int> (std::floor ((position - visibleStart) / 0.25)));
        const float columnWidth = static_cast<float> (stepArea.getWidth())
                                / static_cast<float> (sixteenthColumns);
        auto playhead = juce::Rectangle<float> (
            static_cast<float> (stepArea.getX())
                + columnWidth * static_cast<float> (column),
            static_cast<float> (stepArea.getY()),
            columnWidth,
            static_cast<float> (stepArea.getHeight()));

        g.setColour (juce::Colours::white.withAlpha (0.055f));
        g.fillRect (playhead);
        g.setColour (juce::Colours::white.withAlpha (0.68f));
        g.drawRect (playhead.reduced (0.45f), 1.15f);
    }

    SVDrummerAudioProcessor& processor;
    std::function<void()> onPatternLibraryChanged;
    std::function<void (int)> onLaneSelectionChanged;
    std::function<void()> onLoadCompleted;
    SVDrummerTransportButton enableButton;
    juce::Label midiModeLabel;
    juce::TextButton midiModeButton { "SELECT" };
    juce::Label lengthLabel;
    juce::Slider lengthSlider;
    juce::Label viewLabel;
    juce::Slider viewSlider;
    juce::TextButton laneButton { "LANE 01" };
    juce::Label divisionLabel;
    juce::Slider divisionSlider;
    juce::Label loopLabel;
    juce::Slider loopLengthSlider;
    juce::Label padVolumeLabel;
    juce::Slider padVolumeSlider;
    juce::Label padPanLabel;
    juce::Slider padPanSlider;
    juce::Label padTuneLabel;
    juce::Slider padTuneSlider;
    juce::TextButton patternMenuButton { "MENU" };
    juce::ScrollBar barScroll;
    std::unique_ptr<juce::FileChooser> patternSetSaveChooser;
    std::array<std::unique_ptr<SVDrummerPatternSlot>,
               SVDrummerAudioProcessor::numberOfPatterns> patternSlots;
    std::array<double, SVDrummerAudioProcessor::numberOfPads> resetDivisions {};
    std::array<double, SVDrummerAudioProcessor::numberOfPads> resetLoopLengths {};
    std::array<double, SVDrummerAudioProcessor::numberOfPads> resetPadVolumes {};
    std::array<double, SVDrummerAudioProcessor::numberOfPads> resetPadPans {};
    std::array<double, SVDrummerAudioProcessor::numberOfPads> resetPadTunes {};
    juce::Rectangle<int> rulerBounds;
    juce::Rectangle<int> sequenceRowsBounds;
    juce::Rectangle<int> patternBounds;
    juce::Rectangle<int> padVolumeValueBounds;
    juce::Rectangle<int> padPanValueBounds;
    juce::Rectangle<int> padTuneValueBounds;
    int selectedLane = 0;
    int visibleBars = 1;
    int laneLabelWidth = 74;
    int gestureLane = -1;
    int gestureVelocity = 0;
    int lastGestureVisibleStep = -1;
    int lastDrawVelocity = 100;
    double resetLength = 1.0;
    std::uint64_t resetValuesRevision =
        (std::numeric_limits<std::uint64_t>::max)();
    bool stepGestureActive = false;
    bool updatingControls = false;
};

SVDrummerAudioProcessorEditor::SVDrummerAudioProcessorEditor (
    SVDrummerAudioProcessor& owner)
    : AudioProcessorEditor (&owner), processor (owner)
{
    lookAndFeel = std::make_unique<SVDrummerLookAndFeel>();
    setLookAndFeel (lookAndFeel.get());
    tooltipWindow = std::make_unique<juce::TooltipWindow> (this, 650);
    setOpaque (true);

    logoImage = juce::ImageFileFormat::loadFrom (
        BinaryData::logo_png, BinaryData::logo_pngSize);

    browserPanel = std::make_unique<SVDrummerBrowserPanel> (
        processor,
        [this] { return saveAllSettings(); },
        [this]
        {
            return zoomSavePending || processor.hasUnsavedPortableChanges();
        });
    addAndMakeVisible (*browserPanel);

    for (int padIndex = 0; padIndex < SVDrummerAudioProcessor::numberOfPads; ++padIndex)
    {
        padComponents[static_cast<std::size_t> (padIndex)]
            = std::make_unique<SVDrummerPadComponent> (
                processor, padIndex,
                [this] (int selected) { showPadSettings (selected); },
                [this] (int selected) { selectPadFromIndicator (selected); },
                [this] { handleLibraryLoadCompleted(); });
        addAndMakeVisible (*padComponents[static_cast<std::size_t> (padIndex)]);
    }

    padSettingsPanel = std::make_unique<SVDrummerPadSettingsPanel> (processor);
    sequencerPanel = std::make_unique<SVDrummerSequencerPanel> (
        processor,
        [this]
        {
            if (browserPanel != nullptr)
                browserPanel->refresh();
        },
        [this] (int lane) { updatePadSelection (lane); },
        [this] { handleLibraryLoadCompleted(); });
    addAndMakeVisible (*padSettingsPanel);
    addAndMakeVisible (*sequencerPanel);

    sequencerViewButton.onClick = [this] { showSequencerView(); };
    settingsViewButton.onClick = [this] { showPadSettings (selectedPad); };
    sequencerViewButton.setClickingTogglesState (false);
    settingsViewButton.setClickingTogglesState (false);
    sequencerViewButton.setConnectedEdges (juce::Button::ConnectedOnRight
                                           | juce::Button::ConnectedOnBottom);
    settingsViewButton.setConnectedEdges (juce::Button::ConnectedOnLeft
                                          | juce::Button::ConnectedOnBottom);

    for (auto* tab : { &sequencerViewButton, &settingsViewButton })
    {
        tab->setColour (juce::TextButton::buttonColourId,
                        raisedPanelColour.darker (0.42f));
        tab->setColour (juce::TextButton::buttonOnColourId, panelColour);
        tab->setColour (juce::TextButton::textColourOffId,
                        mutedTextColour.darker (0.12f));
        tab->setColour (juce::TextButton::textColourOnId, textColour);
    }

    addAndMakeVisible (sequencerViewButton);
    addAndMakeVisible (settingsViewButton);

    selectedPad = processor.getEditorSelectedPad();
    sequencerPanel->setSelectedLane (selectedPad);

    if (processor.isEditorShowingPadSettings())
        showPadSettings (selectedPad);
    else
        showSequencerView();

    setResizable (true, true);
    setResizeLimits (baseEditorWidth * 3 / 4, baseEditorHeight * 3 / 4,
                     baseEditorWidth * 2, baseEditorHeight * 2);
    getConstrainer()->setFixedAspectRatio (
        static_cast<double> (baseEditorWidth) / static_cast<double> (baseEditorHeight));
    initialLayoutComplete = true;
    const int savedZoom = loadSavedZoomPercent();
    setSize (juce::roundToInt (static_cast<double> (baseEditorWidth)
                               * static_cast<double> (savedZoom) / 100.0),
             juce::roundToInt (static_cast<double> (baseEditorHeight)
                               * static_cast<double> (savedZoom) / 100.0));
    zoomSavePending = false;
    startTimerHz (20);
}

SVDrummerAudioProcessorEditor::~SVDrummerAudioProcessorEditor()
{
    stopTimer();
    browserPanel->saveUiState();

    if (zoomSavePending)
        saveZoomSetting();

    setLookAndFeel (nullptr);
}

void SVDrummerAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (backgroundColour);
    g.saveState();
    g.addTransform (juce::AffineTransform (
        uiScale, 0.0f, uiOffsetX,
        0.0f, uiScale, uiOffsetY));

    auto header = juce::Rectangle<int> (0, 0, baseEditorWidth, baseEditorHeight)
                      .reduced (10).removeFromTop (42);
    auto title = header.removeFromLeft (250);
    auto logoBounds = title.removeFromLeft (193);

    if (logoImage.isValid())
        g.drawImageWithin (logoImage,
                           logoBounds.getX(), logoBounds.getY(),
                           logoBounds.getWidth(), logoBounds.getHeight(),
                           juce::RectanglePlacement::centred
                               | juce::RectanglePlacement::onlyReduceInSize,
                           false);
    else
    {
        g.setColour (textColour);
        g.setFont (juce::FontOptions (22.0f, juce::Font::bold));
        g.drawText ("SV-DRUMMER", logoBounds,
                    juce::Justification::centredLeft);
    }

    title.removeFromLeft (8);
    g.setColour (juce::Colour (0xff5fa3d1));
    g.setFont (12.0f);
    g.drawSingleLineText ("v0.7.12", title.getX(),
                          logoBounds.getBottom() - 3,
                          juce::Justification::left);
    g.restoreState();
}

void SVDrummerAudioProcessorEditor::resized()
{
    uiScale = juce::jmax (0.01f, juce::jmin (
        static_cast<float> (getWidth()) / static_cast<float> (baseEditorWidth),
        static_cast<float> (getHeight()) / static_cast<float> (baseEditorHeight)));
    uiOffsetX = (static_cast<float> (getWidth())
                 - static_cast<float> (baseEditorWidth) * uiScale) * 0.5f;
    uiOffsetY = (static_cast<float> (getHeight())
                 - static_cast<float> (baseEditorHeight) * uiScale) * 0.5f;

    auto area = juce::Rectangle<int> (0, 0, baseEditorWidth, baseEditorHeight).reduced (10);
    area.removeFromTop (48);

    const int browserWidth = juce::jlimit (245, 390,
                                           static_cast<int> (baseEditorWidth * 0.225f));
    browserPanel->setBounds (area.removeFromLeft (browserWidth));
    area.removeFromLeft (10);

    const int gap = juce::jlimit (3, 7, baseEditorWidth / 210);
    const int availablePadWidth = (area.getWidth() - gap * 7) / 8;
    const int maximumPadHeight = juce::jmax (150, static_cast<int> (area.getHeight() * 0.45f));
    const int padSize = juce::jmax (65, juce::jmin (availablePadWidth,
                                                   (maximumPadHeight - gap) / 2));

    auto padGrid = area.removeFromTop (padSize * 2 + gap);

    for (int row = 0; row < 2; ++row)
    {
        for (int column = 0; column < 8; ++column)
        {
            const int padIndex = row * 8 + column;
            padComponents[static_cast<std::size_t> (padIndex)]->setBounds (
                padGrid.getX() + column * (padSize + gap),
                padGrid.getY() + row * (padSize + gap),
                padSize,
                padSize);
        }
    }

    area.removeFromTop (8);
    auto viewButtons = area.removeFromTop (26);
    sequencerViewButton.setBounds (viewButtons.removeFromLeft (132).withHeight (28));
    settingsViewButton.setBounds (viewButtons.removeFromLeft (132).withHeight (28));

    sequencerPanel->setBounds (area);
    padSettingsPanel->setBounds (area);

    const auto transform = juce::AffineTransform (
        uiScale, 0.0f, uiOffsetX,
        0.0f, uiScale, uiOffsetY);
    browserPanel->setTransform (transform);

    for (auto& pad : padComponents)
        pad->setTransform (transform);

    sequencerViewButton.setTransform (transform);
    settingsViewButton.setTransform (transform);
    sequencerPanel->setTransform (transform);
    padSettingsPanel->setTransform (transform);

    if (initialLayoutComplete)
        zoomSavePending = true;
}

void SVDrummerAudioProcessorEditor::timerCallback()
{
    for (auto& pad : padComponents)
        pad->refreshAnimation();

    if (showingSettings)
        padSettingsPanel->syncFromProcessor();
    else
        sequencerPanel->refresh();

    if (++portableSettingsTimerTicks >= 20)
    {
        portableSettingsTimerTicks = 0;
        browserPanel->saveUiState();
        browserPanel->refreshSaveState();
    }
}

int SVDrummerAudioProcessorEditor::loadSavedZoomPercent() const
{
    return processor.getEditorZoomPercent();
}

juce::Result SVDrummerAudioProcessorEditor::saveZoomSetting() const
{
    if (getWidth() <= 0)
        return juce::Result::fail ("The GUI zoom value is unavailable.");

    const int zoomPercent = juce::jlimit (
        75, 200, juce::roundToInt (static_cast<double> (uiScale) * 100.0));
    processor.setEditorZoomPercent (zoomPercent);
    return processor.saveEditorZoomNow();
}

juce::Result SVDrummerAudioProcessorEditor::saveAllSettings()
{
    const auto portableResult = processor.savePortableSettingsNow();

    if (portableResult.failed())
        return portableResult;

    const auto zoomResult = saveZoomSetting();

    if (zoomResult.failed())
        return zoomResult;

    zoomSavePending = false;
    return juce::Result::ok();
}

void SVDrummerAudioProcessorEditor::handleLibraryLoadCompleted()
{
    for (auto& pad : padComponents)
        pad->repaint();

    padSettingsPanel->syncFromProcessor();
    sequencerPanel->refresh();
    browserPanel->refresh (false);
    offerToRelinkMissingSamples();
}

void SVDrummerAudioProcessorEditor::offerToRelinkMissingSamples()
{
    const auto missing = processor.getMissingSampleDescriptions();

    if (missing.isEmpty())
        return;

    juce::String message;
    message << juce::String (missing.size())
            << (missing.size() == 1 ? " sample could" : " samples could")
            << " not be found:\n\n"
            << missing.joinIntoString ("\n")
            << "\n\nLocate the folder containing the missing sample"
            << (missing.size() == 1 ? "?" : "s?");

    juce::Component::SafePointer<SVDrummerAudioProcessorEditor> safeThis (this);
    juce::AlertWindow::showOkCancelBox (
        juce::MessageBoxIconType::WarningIcon,
        "Missing Samples",
        message,
        "Locate Folder", "Leave Missing", this,
        juce::ModalCallbackFunction::create (
            [safeThis] (int result)
            {
                if (safeThis != nullptr && result != 0)
                    safeThis->chooseMissingSampleFolder();
            }));
}

void SVDrummerAudioProcessorEditor::chooseMissingSampleFolder()
{
    auto initialFolder = processor.getPortableSamplesDirectory();

    if (! initialFolder.isDirectory())
        initialFolder = processor.getPortableDataDirectory();

    missingSampleFolderChooser = std::make_unique<juce::FileChooser> (
        "Locate Missing SV-Drummer Samples", initialFolder, "*", true);
    juce::Component::SafePointer<SVDrummerAudioProcessorEditor> safeThis (this);

    missingSampleFolderChooser->launchAsync (
        juce::FileBrowserComponent::openMode
            | juce::FileBrowserComponent::canSelectDirectories,
        [safeThis] (const juce::FileChooser& chooser)
        {
            if (safeThis == nullptr)
                return;

            const auto folder = chooser.getResult();

            if (! folder.isDirectory())
                return;

            const int relinked =
                safeThis->processor.relinkMissingSamplesFromFolder (folder);
            const auto remaining =
                safeThis->processor.getMissingSampleDescriptions();

            for (auto& pad : safeThis->padComponents)
                pad->repaint();

            safeThis->padSettingsPanel->syncFromProcessor();
            safeThis->sequencerPanel->refresh();

            if (remaining.isEmpty())
            {
                juce::AlertWindow::showMessageBoxAsync (
                    juce::MessageBoxIconType::InfoIcon,
                    "SV-Drummer Samples Relinked",
                    juce::String (relinked)
                        + (relinked == 1 ? " sample was" : " samples were")
                        + " found and relinked.");
                return;
            }

            juce::String message;
            message << juce::String (relinked)
                    << (relinked == 1 ? " sample was" : " samples were")
                    << " relinked.\n\nStill missing:\n\n"
                    << remaining.joinIntoString ("\n");
            juce::AlertWindow::showMessageBoxAsync (
                juce::MessageBoxIconType::WarningIcon,
                "Some Samples Are Still Missing", message);
        });
}

void SVDrummerAudioProcessorEditor::showSequencerView()
{
    showingSettings = false;
    processor.setEditorShowingPadSettings (false);
    updatePadSelection (sequencerPanel->getSelectedLane());

    sequencerPanel->setVisible (true);
    padSettingsPanel->setVisible (false);
    updateViewButtons();
}

void SVDrummerAudioProcessorEditor::showPadSettings (int padIndex)
{
    selectedPad = juce::jlimit (0, SVDrummerAudioProcessor::numberOfPads - 1,
                               padIndex);
    showingSettings = true;
    processor.setEditorShowingPadSettings (true);
    sequencerPanel->setSelectedLane (selectedPad);
    updatePadSelection (selectedPad);

    padSettingsPanel->setPadIndex (selectedPad);
    sequencerPanel->setVisible (false);
    padSettingsPanel->setVisible (true);
    updateViewButtons();
}

void SVDrummerAudioProcessorEditor::selectPadFromIndicator (int padIndex)
{
    const int selected = juce::jlimit (
        0, SVDrummerAudioProcessor::numberOfPads - 1, padIndex);

    if (showingSettings)
    {
        showPadSettings (selected);
        return;
    }

    sequencerPanel->setSelectedLane (selected);
    updatePadSelection (selected);
}

void SVDrummerAudioProcessorEditor::updatePadSelection (int padIndex)
{
    selectedPad = juce::jlimit (
        0, SVDrummerAudioProcessor::numberOfPads - 1, padIndex);
    processor.setEditorSelectedPad (selectedPad);

    for (int index = 0; index < SVDrummerAudioProcessor::numberOfPads; ++index)
        padComponents[static_cast<std::size_t> (index)]->setSelected (
            index == selectedPad);
}

void SVDrummerAudioProcessorEditor::updateViewButtons()
{
    sequencerViewButton.setToggleState (! showingSettings, juce::dontSendNotification);
    settingsViewButton.setToggleState (showingSettings, juce::dontSendNotification);
    sequencerViewButton.repaint();
    settingsViewButton.repaint();
    repaint();
}
