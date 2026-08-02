// エディタを実際に描画してPNGに書き出す。
// レイアウトは座標を手で組んでいるので、見ずに「こうなっているはず」と言わないための道具。
//
//   ShellSpaceUI.exe <out.png>
//
// VST3経由ではなくプラグインのソースを直接リンクしている。
// VST3のエディタはネイティブ子ウィンドウなので、ホスト経由だとスナップショットが取れないため。

#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"

#include <iostream>
#include <functional>

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    const juce::File out (argc >= 2 ? juce::File::getCurrentWorkingDirectory()
                                          .getChildFile (juce::String::fromUTF8 (argv[1]))
                                    : juce::File::getCurrentWorkingDirectory()
                                          .getChildFile ("shellspace_ui.png"));

    ShellSpaceProcessor proc;
    proc.prepareToPlay (48000.0, 512);

    std::unique_ptr<juce::AudioProcessorEditor> editor (proc.createEditor());
    if (editor == nullptr)
    {
        std::cout << "[FAIL] エディタが生成できません" << std::endl;
        return 1;
    }

    editor->resized();

    std::cout << "editor size: " << editor->getWidth() << " x " << editor->getHeight() << std::endl;

    // 中身がはみ出していないか調べる（子要素の外接矩形とエディタ矩形を比べる）
    juce::Rectangle<int> childUnion;
    for (auto* c : editor->getChildren())
        childUnion = childUnion.getUnion (c->getBounds());

    std::cout << "children bounds: " << childUnion.toString() << std::endl;

    const auto frame = editor->getLocalBounds();
    const bool fits = frame.contains (childUnion);
    std::cout << (fits ? "[OK]   " : "[WARN] ")
              << "子要素がエディタ内に収まっている" << std::endl;

    if (! fits)
    {
        std::cout << "       はみ出し: 左" << (frame.getX() - childUnion.getX())
                  << " 上" << (frame.getY() - childUnion.getY())
                  << " 右" << (childUnion.getRight() - frame.getRight())
                  << " 下" << (childUnion.getBottom() - frame.getBottom()) << std::endl;
    }

    int editableDbLabels = 0;
    std::function<void (juce::Component*)> countEditable = [&] (juce::Component* parent)
    {
        for (auto* child : parent->getChildren())
        {
            if (auto* fader = dynamic_cast<Fader*> (child);
                fader != nullptr && fader->value.isEditableOnDoubleClick())
                ++editableDbLabels;
            countEditable (child);
        }
    };
    countEditable (editor.get());
    std::cout << (editableDbLabels == 4 ? "[OK]   " : "[FAIL] ")
              << "dB数値入力ラベル: " << editableDbLabels << "/4" << std::endl;

    // 重なりの検出（ノブ同士が被っていたらレイアウトミス）
    auto kids = editor->getChildren();
    for (int i = 0; i < kids.size(); ++i)
        for (int j = i + 1; j < kids.size(); ++j)
            if (kids[i]->getBounds().intersects (kids[j]->getBounds()))
                std::cout << "[WARN] 要素が重なっています: "
                          << kids[i]->getBounds().toString() << " / "
                          << kids[j]->getBounds().toString() << std::endl;

    const auto image = editor->createComponentSnapshot (frame, true, 2.0f);

    if (out.existsAsFile())
        out.deleteFile();

    if (auto stream = out.createOutputStream())
    {
        juce::PNGImageFormat png;
        if (png.writeImageToStream (image, *stream))
        {
            stream->flush();
            std::cout << "[OK]   wrote " << out.getFullPathName()
                      << "  (" << image.getWidth() << "x" << image.getHeight() << ")" << std::endl;
            return fits && editableDbLabels == 4 ? 0 : 1;
        }
    }

    std::cout << "[FAIL] PNGを書き出せません" << std::endl;
    return 1;
}
