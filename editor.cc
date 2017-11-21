// Copyright 2017 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
#include "editor.h"

EditResponse Editor::MakeResponse() {
  EditResponse r;
  r.done = state_.shutdown;
  r.become_used = !unpublished_commands_.commands().empty();
  state_.content = state_.content.Integrate(unpublished_commands_);
  r.content_updates.MergeFrom(unpublished_commands_);
  unacknowledged_commands_.MergeFrom(unpublished_commands_);
  unpublished_commands_.Clear();
  AnnotationEditor::ScopedEdit edit(&ed_, &r.content_updates);
  Attribute curs;
  curs.mutable_cursor();
  ed_.Mark(cursor_,
           AnnotatedString::Iterator(state_.content, cursor_).Next().id(),
           curs);
  assert(unpublished_commands_.commands().empty());
  return r;
}

void Editor::UpdateState(const EditNotification& state) {
  state_ = state;
  auto s2 = state_.content.Integrate(unacknowledged_commands_);
  if (s2.SameTotalIdentity(state_.content)) {
    unacknowledged_commands_.Clear();
  } else {
    state_.content = s2;
  }
}

void Editor::SelectLeft() {
  SetSelectMode(true);
  CursorLeft();
}

void Editor::MoveLeft() {
  SetSelectMode(false);
  CursorLeft();
}

void Editor::SelectRight() {
  SetSelectMode(true);
  CursorRight();
}

void Editor::MoveRight() {
  SetSelectMode(false);
  CursorRight();
}

void Editor::MoveStartOfLine() {
  SetSelectMode(false);
  CursorStartOfLine();
}

void Editor::MoveEndOfLine() {
  SetSelectMode(false);
  CursorEndOfLine();
}

void Editor::MoveDownN(int n) {
  SetSelectMode(false);
  for (int i = 0; i < n; i++) CursorDown();
}

void Editor::MoveUpN(int n) {
  SetSelectMode(false);
  for (int i = 0; i < n; i++) CursorUp();
}

void Editor::SelectDownN(int n) {
  SetSelectMode(true);
  for (int i = 0; i < n; i++) CursorDown();
}

void Editor::SelectUpN(int n) {
  SetSelectMode(true);
  for (int i = 0; i < n; i++) CursorUp();
}

void Editor::Backspace() {
  SetSelectMode(false);
  state_.content.MakeDelete(&unpublished_commands_, cursor_);
  AnnotatedString::Iterator it(state_.content, cursor_);
  it.MovePrev();
  cursor_ = it.id();
}

void Editor::Copy(AppEnv* env) {
  if (SelectMode()) {
    env->clipboard = state_.content.Render(cursor_, selection_anchor_);
  }
}

void Editor::Cut(AppEnv* env) {
  if (SelectMode()) {
    env->clipboard = state_.content.Render(cursor_, selection_anchor_);
    DeleteSelection();
    SetSelectMode(false);
  }
}

void Editor::Paste(AppEnv* env) {
  if (selection_anchor_ != ID()) {
    DeleteSelection();
    SetSelectMode(false);
  }
  cursor_ = state_.content.MakeInsert(&unpublished_commands_, site_,
                                      env->clipboard, cursor_);
}

void Editor::InsChar(char c) {
  DeleteSelection();
  SetSelectMode(false);
  cursor_ = state_.content.MakeInsert(&unpublished_commands_, site_,
                                      absl::string_view(&c, 1), cursor_);
  cursor_row_ += (c == '\n');
}

void Editor::SetSelectMode(bool sel) {
  if (!sel) {
    selection_anchor_ = ID();
  } else if (selection_anchor_ == ID()) {
    selection_anchor_ = cursor_;
  }
}

void Editor::DeleteSelection() {
  if (!SelectMode()) return;
  state_.content.MakeDelete(&unpublished_commands_, cursor_, selection_anchor_);
  AnnotatedString::Iterator it(state_.content, cursor_);
  it.MovePrev();
  cursor_ = it.id();
}

void Editor::CursorLeft() {
  AnnotatedString::Iterator it(state_.content, cursor_);
  cursor_row_ -= it.value() == '\n';
  it.MovePrev();
  cursor_ = it.id();
}

void Editor::CursorRight() {
  AnnotatedString::Iterator it(state_.content, cursor_);
  it.MoveNext();
  cursor_row_ += it.value() == '\n';
  cursor_ = it.id();
}

void Editor::CursorDown() {
  AnnotatedString::Iterator it(state_.content, cursor_);
  int col = 0;
  auto edge = [&it]() {
    return it.is_begin() || it.is_end() || it.value() == '\n';
  };
  while (!edge()) {
    it.MovePrev();
    col++;
  }
  Log() << "col:" << col;
  it = AnnotatedString::Iterator(state_.content, cursor_);
  do {
    it.MoveNext();
  } while (!edge());
  it.MoveNext();
  for (; col > 0 && !edge(); col--) {
    it.MoveNext();
  }
  it.MovePrev();
  cursor_ = it.id();
  cursor_row_++;
}

void Editor::CursorUp() {
  AnnotatedString::Iterator it(state_.content, cursor_);
  int col = 0;
  auto edge = [&it]() {
    return it.is_begin() || it.is_end() || it.value() == '\n';
  };
  while (!edge()) {
    it.MovePrev();
    col++;
  }
  Log() << "col:" << col;
  do {
    it.MovePrev();
  } while (!edge());
  it.MoveNext();
  for (; col > 0 && !edge(); col--) {
    it.MoveNext();
  }
  it.MovePrev();
  cursor_ = it.id();
  cursor_row_--;
}

void Editor::CursorStartOfLine() {
  cursor_ = AnnotatedString::LineIterator(state_.content, cursor_).id();
}

void Editor::CursorEndOfLine() {
  cursor_ = AnnotatedString::LineIterator(state_.content, cursor_)
                .Next()
                .AsIterator()
                .Prev()
                .id();
}

int Editor::RenderCommon(
    int window_height,
    std::function<void(LineInfo, const std::vector<CharInfo>&)> add_line) {
  if (cursor_row_ < 0) {
    cursor_row_ = 0;
  } else if (cursor_row_ >= window_height) {
    cursor_row_ = window_height - 1;
  }

  cursor_ = AnnotatedString::Iterator(state_.content, cursor_).id();
  AnnotatedString::LineIterator line_cr(state_.content, cursor_);
  AnnotatedString::LineIterator line_end_cr = line_cr.Next();
  AnnotatedString::LineIterator line_bk = line_cr;
  AnnotatedString::LineIterator line_fw = line_cr;
  for (int i = 0; i < window_height; i++) {
    line_bk.MovePrev();
    line_fw.MoveNext();
  }
  AnnotatedString::AllIterator it = line_bk.AsAllIterator();
  AnnotationTracker<Tag> t_token(state_.token_types);
  AnnotationTracker<ID> t_diagnostic(state_.diagnostic_ranges);
  AnnotationTracker<SideBufferRef> t_side_buffer_ref(state_.side_buffer_refs);
  bool in_selection = false;
  std::vector<CharInfo> ci;
  int nrow = 0;
  int ncol = 0;
  auto last_was_cursor = [&]() {
    return AnnotatedString::Iterator(state_.content, it.id()).Prev().id() ==
           cursor_;
  };
  std::vector<std::string> gutter_annotations;
  while (it.id() != line_fw.id()) {
    t_token.Enter(it.id());
    t_diagnostic.Enter(it.id());
    t_side_buffer_ref.Enter(it.id());
    if (SelectMode() && it.id() == selection_anchor_) {
      in_selection = !in_selection;
    }
    if (it.id() == cursor_) {
      if (SelectMode()) in_selection = !in_selection;
      cursor_token_ = t_token.cur();
      if (!t_side_buffer_ref.cur().name.empty()) {
        active_side_buffer_ = t_side_buffer_ref.cur();
      } else {
        active_side_buffer_.lines.clear();
      }
    }
    state_.gutter_notes.ForEachValue(it.id(), [&](const std::string& note) {
      gutter_annotations.push_back(note);
    });

    if (it.is_visible()) {
      if (it.value() == '\n') {
        LineInfo li{it.id() == line_end_cr.id(),
                    absl::StrJoin(gutter_annotations, ",")};
        gutter_annotations.clear();
        ci.emplace_back(CharInfo{' ', false, last_was_cursor(), Tag()});
        nrow++;
        ncol = 0;
        add_line(li, ci);
        ci.clear();
      } else {
        ncol++;
        Tag tok = t_token.cur();
        if (t_diagnostic.cur() != ID()) {
          tok = tok.Push("error");
        }
        ci.emplace_back(
            CharInfo{it.value(), in_selection, last_was_cursor(), tok});
      }
    }
    it.MoveNext();
  }
  return -1;
}

int Editor::RenderSideBarCommon(int window_height, LineCallback callback) {
  int first_line = 0;
  state_.side_buffers.ForEachValue(
      active_side_buffer_.name, [&](const SideBuffer& buf) {
        std::vector<CharInfo> ci;
        first_line = 0;
        int last_line = buf.line_ofs.size() - 1;
        if (first_line < -window_height - last_sb_offset_) {
          first_line = -window_height - last_sb_offset_;
        }
        if (last_line > first_line + 2 * window_height) {
          last_line = first_line + 2 * window_height;
        }
        Log() << "last_sb_offset_=" << last_sb_offset_
              << " first_line=" << first_line << " last_line=" << last_line
              << " line_ofs.size()=" << buf.line_ofs.size();
        for (size_t line = first_line; line < last_line; line++) {
          size_t line_beg = buf.line_ofs[line];
          size_t line_end = buf.line_ofs[line + 1];
          ci.clear();
          ci.reserve(line_end - line_beg);
          for (size_t i = line_beg; i < line_end; i++) {
            ci.emplace_back(
                CharInfo{buf.content[i] == '\n' ? ' ' : buf.content[i], false,
                         false, Tag()});
          }
          callback(LineInfo{std::find(active_side_buffer_.lines.begin(),
                                      active_side_buffer_.lines.end(),
                                      line) != active_side_buffer_.lines.end(),
                            std::string()},
                   ci);
        }
      });
  return first_line;
}
