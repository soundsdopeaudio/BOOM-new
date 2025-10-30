#pragma once

int mgCols_ = 16;                 // steps
int mgRows_ = 8;                  // lanes
juce::Array<bool> mgData_;        // size mgCols_*mgRows_
int mgCellW_ = 12, mgCellH_ = 10; // tweak
int mgLeft_ = 12, mgTop_ = 12;

std::function<void(const juce::Array<bool>&)> onGridChanged;


void MiniGrid::mouseDown(const juce::MouseEvent& e)
{
    const int c = (e.x - mgLeft_) / mgCellW_;
    const int r = (e.y - mgTop_) / mgCellH_;
    if (c < 0 || c >= mgCols_ || r < 0 || r >= mgRows_) return;
    const int idx = r * mgCols_ + c;
    mgData_.set(idx, !mgData_[idx]);
    if (onGridChanged) onGridChanged(mgData_);
    repaint();
}

void MiniGrid::mouseDrag(const juce::MouseEvent& e) { mouseDown(e); }
