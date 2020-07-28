#include <algorithm>
#include <chrono>
#include <string>
#include <regex>
#include <cmath>
#define NOMINMAX
#include "TextEditor.h"

#include <windows.h>

#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui.h" // for imGui::GetCurrentWindow()

// TODO
// - multiline comments vs single-line: latter is blocking start of a ML
// - handle unicode/utf
// - testing

template<class InputIt1, class InputIt2, class BinaryPredicate>
bool equals(InputIt1 first1, InputIt1 last1,
	InputIt2 first2, InputIt2 last2, BinaryPredicate p)
{
	for (; first1 != last1 && first2 != last2; ++first1, ++first2)
	{
		if (!p(*first1, *first2))
			return false;
	}
	return first1 == last1 && first2 == last2;
}

TextEditor::TextEditor()
	: mLineSpacing(1.0f)
	, mUndoIndex(0)
	, mTabSize(4)
	, mOverwrite(false)
	, mReadOnly(false)
	, mWithinRender(false)
	, mScrollToCursor(false)
	, mScrollToTop(false)
	, mTextChanged(false)
	, mTextStart(20.0f)
	, mLeftMargin(10)
	, mColorRangeMin(0)
	, mColorRangeMax(0)
	, mSelectionMode(SelectionMode::Normal)
	, mCheckComments(true)
	, mLastClick(-1.0f)
{
	SetPalette(GetDarkPalette());
	SetLanguageDefinition(LanguageDefinition::HLSL());
	mLines.push_back(Line());
}

TextEditor::~TextEditor()
{
}

void TextEditor::SetLanguageDefinition(const LanguageDefinition & aLanguageDef)
{
	mLanguageDefinition = aLanguageDef;
	mRegexList.clear();

	for (auto& r : mLanguageDefinition.mTokenRegexStrings)
		mRegexList.push_back(std::make_pair(std::wregex(r.first, std::regex_constants::optimize), r.second));
}

void TextEditor::SetPalette(const Palette & aValue)
{
	mPaletteBase = aValue;
}

int TextEditor::AppendBuffer(std::wstring& aBuffer, Char chr, int aIndex)
{
	if (chr != L'\t')
	{
		aBuffer.push_back(chr);
		return aIndex + 1;
	}
	else
	{
		//auto num = mTabSize - aIndex % mTabSize;
		//for (int j = num; j > 0; --j)
		//	aBuffer.push_back(' ');
		//return aIndex + num;
		return aIndex;
	}
}

std::wstring TextEditor::GetText(const Coordinates & aStart, const Coordinates & aEnd) const
{
	std::wstring result;

	int prevLineNo = aStart.mLine;
	for (auto it = aStart; it <= aEnd; Advance(it))
	{
		if (prevLineNo != it.mLine && it.mLine < (int)mLines.size())
			result.push_back(L'\n');

		if (it == aEnd)
			break;

		prevLineNo = it.mLine;
		const auto& line = mLines[it.mLine];
		if (!line.empty() && it.mColumn < (int)line.size())
			result.push_back(line[it.mColumn].mChar);
	}

	return result;
}

TextEditor::Coordinates TextEditor::GetActualCursorCoordinates() const
{
	return SanitizeCoordinates(mState.mCursorPosition);
}

TextEditor::Coordinates TextEditor::SanitizeCoordinates(const Coordinates & aValue) const
{
	auto line = aValue.mLine;
	auto column = aValue.mColumn;

	if (line >= (int)mLines.size())
	{
		if (mLines.empty())
		{
			line = 0;
			column = 0;
		}
		else
		{
			line = (int)mLines.size() - 1;
			
			column = (int)mLines[line].size();
		}
	}
	else
	{
		column = mLines.empty() ? 0 : std::min((int)mLines[line].size(), aValue.mColumn);
	}

	return Coordinates(line, column);
}

void TextEditor::Advance(Coordinates & aCoordinates) const
{
	if (aCoordinates.mLine < (int)mLines.size())
	{
		auto& line = mLines[aCoordinates.mLine];

		if (aCoordinates.mColumn + 1 < (int)line.size())
			++aCoordinates.mColumn;
		else
		{
			++aCoordinates.mLine;
			aCoordinates.mColumn = 0;
		}
	}
}

void TextEditor::DeleteRange(const Coordinates & aStart, const Coordinates & aEnd)
{
	assert(aEnd >= aStart);
	assert(!mReadOnly);

	if (aEnd == aStart)
		return;

	if (aStart.mLine == aEnd.mLine)
	{
		auto& line = mLines[aStart.mLine];
		if (aEnd.mColumn >= (int)line.size())
			line.erase(line.begin() + aStart.mColumn, line.end());
		else
			line.erase(line.begin() + aStart.mColumn, line.begin() + aEnd.mColumn);
	}
	else
	{
		auto& firstLine = mLines[aStart.mLine];
		auto& lastLine = mLines[aEnd.mLine];

		firstLine.erase(firstLine.begin() + aStart.mColumn, firstLine.end());
		lastLine.erase(lastLine.begin(), lastLine.begin() + aEnd.mColumn);

		if (aStart.mLine < aEnd.mLine)
			firstLine.insert(firstLine.end(), lastLine.begin(), lastLine.end());

		if (aStart.mLine < aEnd.mLine)
			RemoveLine(aStart.mLine + 1, aEnd.mLine + 1);
	}

	mTextChanged = true;
}

int TextEditor::InsertTextAt(Coordinates& /* inout */ aWhere, const Char * aValue)
{
	assert(!mReadOnly);

	int totalLines = 0;
	auto chr = *aValue;
	while (chr != L'\0')
	{
		assert(!mLines.empty());

		if (chr == L'\r')
		{
			// skip
		}
		else if (chr == L'\n')
		{
			if (aWhere.mColumn < (int)mLines[aWhere.mLine].size())
			{
				auto& newLine = InsertLine(aWhere.mLine + 1);
				auto& line = mLines[aWhere.mLine];
				newLine.insert(newLine.begin(), line.begin() + aWhere.mColumn, line.end());
				line.erase(line.begin() + aWhere.mColumn, line.end());
			}
			else
			{
				InsertLine(aWhere.mLine + 1);
			}
			++aWhere.mLine;
			aWhere.mColumn = 0;
			++totalLines;
		}
		else
		{
			auto& line = mLines[aWhere.mLine];
			line.insert(line.begin() + aWhere.mColumn, Glyph(chr, PaletteIndex::Default));
			++aWhere.mColumn;
		}
		chr = *(++aValue);

		mTextChanged = true;
	}

	return totalLines;
}

void TextEditor::AddUndo(UndoRecord& aValue)
{
	assert(!mReadOnly);

	mUndoBuffer.resize(mUndoIndex + 1);
	mUndoBuffer.back() = aValue;
	++mUndoIndex;
}

TextEditor::Coordinates TextEditor::ScreenPosToCoordinates(const ImVec2& aPosition) const
{
	ImVec2 origin = ImGui::GetCursorScreenPos();
	ImVec2 local(aPosition.x - origin.x, aPosition.y - origin.y);

	int lineNo = std::max(0, (int)floor(local.y / mCharAdvance.y));

	/*
		Compute columnCoord according to text size
	*/
	int columnCoord = 0;
	float columnWidth = 0.0f;
	std::wstring cumulatedString = L"";
	float cumulatedStringWidth[2] = { 0.0f, 0.0f }; //( [0] is the lastest, [1] is the previous. I use that trick to check where cursor is exactly (important for tabs)

	if (lineNo >= 0 && lineNo < (int)mLines.size())
	{
		auto& line = mLines.at(lineNo);

		// First we find the hovered column coord.
		while (mTextStart + cumulatedStringWidth[0] < local.x &&
			(size_t)columnCoord < line.size())
		{
			cumulatedStringWidth[1] = cumulatedStringWidth[0];
			cumulatedString += line[columnCoord].mChar;
			cumulatedStringWidth[0] = ImGui::GetFont()->CalcTextSizeA(ImGui::GetFontSize(), FLT_MAX, -1.0f, (char*)cumulatedString.c_str(), nullptr, nullptr).x;
			columnWidth = (cumulatedStringWidth[0] - cumulatedStringWidth[1]);
			columnCoord++;
		}

		// Then we reduce by 1 column coord if cursor is on the left side of the hovered column.
		if (mTextStart + cumulatedStringWidth[0] - columnWidth / 2.0f > local.x)
			columnCoord = std::max(0, columnCoord - 1);
	}

	return SanitizeCoordinates(Coordinates(lineNo, columnCoord));
}

TextEditor::Coordinates TextEditor::FindWordStart(const Coordinates & aFrom) const
{
	Coordinates at = aFrom;
	if (at.mLine >= (int)mLines.size())
		return at;

	auto& line = mLines[at.mLine];

	if (at.mColumn >= (int)line.size())
		return at;

	auto cstart = (PaletteIndex)line[at.mColumn].mColorIndex;
	while (at.mColumn > 0)
	{
		if (cstart != (PaletteIndex)line[at.mColumn - 1].mColorIndex)
			break;
		--at.mColumn;
	}
	return at;
}

TextEditor::Coordinates TextEditor::FindWordEnd(const Coordinates & aFrom) const
{
	Coordinates at = aFrom;
	if (at.mLine >= (int)mLines.size())
		return at;

	auto& line = mLines[at.mLine];

	if (at.mColumn >= (int)line.size())
		return at;

	auto cstart = (PaletteIndex)line[at.mColumn].mColorIndex;
	while (at.mColumn < (int)line.size())
	{
		if (cstart != (PaletteIndex)line[at.mColumn].mColorIndex)
			break;
		++at.mColumn;
	}
	return at;
}

bool TextEditor::IsOnWordBoundary(const Coordinates & aAt) const
{
	if (aAt.mLine >= (int)mLines.size() || aAt.mColumn == 0)
		return true;

	auto& line = mLines[aAt.mLine];
	if (aAt.mColumn >= (int)line.size())
		return true;

	return line[aAt.mColumn].mColorIndex != line[aAt.mColumn - 1].mColorIndex;
}

void TextEditor::RemoveLine(int aStart, int aEnd)
{
	assert(!mReadOnly);
	assert(aEnd >= aStart);
	assert(mLines.size() > (size_t)(aEnd - aStart));

	ErrorMarkers etmp;
	for (auto& i : mErrorMarkers)
	{
		ErrorMarkers::value_type e(i.first >= aStart ? i.first - 1 : i.first, i.second);
		if (e.first >= aStart && e.first <= aEnd)
			continue;
		etmp.insert(e);
	}
	mErrorMarkers = std::move(etmp);

	Breakpoints btmp;
	for (auto i : mBreakpoints)
	{
		if (i >= aStart && i <= aEnd)
			continue;
		btmp.insert(i >= aStart ? i - 1 : i);
	}
	mBreakpoints = std::move(btmp);

	mLines.erase(mLines.begin() + aStart, mLines.begin() + aEnd);
	assert(!mLines.empty());

	mTextChanged = true;
}

void TextEditor::RemoveLine(int aIndex)
{
	assert(!mReadOnly);
	assert(mLines.size() > 1);

	ErrorMarkers etmp;
	for (auto& i : mErrorMarkers)
	{
		ErrorMarkers::value_type e(i.first > aIndex ? i.first - 1 : i.first, i.second);
		if (e.first - 1 == aIndex)
			continue;
		etmp.insert(e);
	}
	mErrorMarkers = std::move(etmp);

	Breakpoints btmp;
	for (auto i : mBreakpoints)
	{
		if (i == aIndex)
			continue;
		btmp.insert(i >= aIndex ? i - 1 : i);
	}
	mBreakpoints = std::move(btmp);

	mLines.erase(mLines.begin() + aIndex);
	assert(!mLines.empty());

	mTextChanged = true;
}

TextEditor::Line& TextEditor::InsertLine(int aIndex)
{
	assert(!mReadOnly);

	auto& result = *mLines.insert(mLines.begin() + aIndex, Line());

	ErrorMarkers etmp;
	for (auto& i : mErrorMarkers)
		etmp.insert(ErrorMarkers::value_type(i.first >= aIndex ? i.first + 1 : i.first, i.second));
	mErrorMarkers = std::move(etmp);

	Breakpoints btmp;
	for (auto i : mBreakpoints)
		btmp.insert(i >= aIndex ? i + 1 : i);
	mBreakpoints = std::move(btmp);

	return result;
}

std::wstring TextEditor::GetWordUnderCursor() const
{
	auto c = GetCursorPosition();
	return GetWordAt(c);
}

std::wstring TextEditor::GetWordAt(const Coordinates & aCoords) const
{
	auto start = FindWordStart(aCoords);
	auto end = FindWordEnd(aCoords);

	std::wstring r;

	for (auto it = start; it < end; Advance(it))
		r.push_back(mLines[it.mLine][it.mColumn].mChar);

	return r;
}

ImU32 TextEditor::GetGlyphColor(const Glyph & aGlyph) const
{
	if (aGlyph.mComment)
		return mPalette[(int)PaletteIndex::Comment];
	if (aGlyph.mMultiLineComment)
		return mPalette[(int)PaletteIndex::MultiLineComment];
	auto const color = mPalette[(int)aGlyph.mColorIndex];
	if (aGlyph.mPreprocessor)
	{
		const auto ppcolor = mPalette[(int)PaletteIndex::Preprocessor];
		const int c0 = ((ppcolor & 0xff) + (color & 0xff)) / 2;
		const int c1 = (((ppcolor >> 8) & 0xff) + ((color >> 8) & 0xff)) / 2;
		const int c2 = (((ppcolor >> 16) & 0xff) + ((color >> 16) & 0xff)) / 2;
		const int c3 = (((ppcolor >> 24) & 0xff) + ((color >> 24) & 0xff)) / 2;
		return ImU32(c0 | (c1 << 8) | (c2 << 16) | (c3 << 24));
	}

	return color;
}

#include <iostream>

void TextEditor::HandleKeyboardInputs()
{
	ImGuiIO& io = ImGui::GetIO();
	auto shift = io.KeyShift;
	auto ctrl = io.ConfigMacOSXBehaviors ? io.KeySuper : io.KeyCtrl;
	auto alt = io.ConfigMacOSXBehaviors ? io.KeyCtrl : io.KeyAlt;

	if (ImGui::IsWindowFocused())
	{
		if (ImGui::IsWindowHovered())
			ImGui::SetMouseCursor(ImGuiMouseCursor_TextInput);
		//ImGui::CaptureKeyboardFromApp(true);

		io.WantCaptureKeyboard = true;
		io.WantTextInput = true;

		if (!IsReadOnly() && ctrl && !shift && !alt && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Z)))
			Undo();
		else if (!IsReadOnly() && !ctrl && !shift && alt && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Backspace)))
			Undo();
		else if (!IsReadOnly() && ctrl && !shift && !alt && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Y)))
			Redo();
		else if (!ctrl && !alt && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_UpArrow)))
			MoveUp(1, shift);
		else if (!ctrl && !alt && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_DownArrow)))
			MoveDown(1, shift);
		else if (!alt && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_LeftArrow)))
			MoveLeft(1, shift, ctrl);
		else if (!alt && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_RightArrow)))
			MoveRight(1, shift, ctrl);
		else if (!alt && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_PageUp)))
			MoveUp(GetPageSize() - 4, shift);
		else if (!alt && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_PageDown)))
			MoveDown(GetPageSize() - 4, shift);
		else if (!alt && ctrl && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Home)))
			MoveTop(shift);
		else if (ctrl && !alt && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_End)))
			MoveBottom(shift);
		else if (!ctrl && !alt && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Home)))
			MoveHome(shift);
		else if (!ctrl && !alt && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_End)))
			MoveEnd(shift);
		else if (!IsReadOnly() && !ctrl && !shift && !alt && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Delete)))
			Delete();
		else if (!IsReadOnly() && !ctrl && !shift && !alt && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Backspace)))
			BackSpace();
		else if (!ctrl && !shift && !alt && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Insert)))
			mOverwrite ^= true;
		else if (ctrl && !shift && !alt && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Insert)))
			Copy();
		else if (ctrl && !shift && !alt && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_C)))
			Copy();
		else if (!IsReadOnly() && !ctrl && shift && !alt && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Insert)))
			Paste();
		else if (!IsReadOnly() && ctrl && !shift && !alt && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_V)))
			Paste();
		else if (ctrl && !shift && !alt && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_X)))
			Cut();
		else if (!ctrl && shift && !alt && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Delete)))
			Cut();
		else if (ctrl && !shift && !alt && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_A)))
			SelectAll();
		else if (!IsReadOnly() && !ctrl && !shift && !alt && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Enter)))
			EnterCharacter(L'\n', false);
		else if (!IsReadOnly() && !ctrl && !alt && ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Tab)))
			EnterCharacter(L'\t', shift);
		else if (!IsReadOnly() && !ctrl && !alt)
		{
			for (int i = 0; i < io.InputQueueCharacters.Size; i++)
			{
				auto c = io.InputQueueCharacters[i];				
				if (c != 0)
				{
					if (isprint(c) || isspace(c))
					{						
						EnterCharacter(c, shift);
					}
				}
				std::wcout << (Char)c << std::endl;
			}
			io.InputQueueCharacters.resize(0);
		}
	}
}

void TextEditor::HandleMouseInputs()
{
	ImGuiIO& io = ImGui::GetIO();
	auto shift = io.KeyShift;
	auto ctrl = io.ConfigMacOSXBehaviors ? io.KeySuper : io.KeyCtrl;
	auto alt = io.ConfigMacOSXBehaviors ? io.KeyCtrl : io.KeyAlt;

	if (ImGui::IsWindowHovered())
	{
		if (!shift && !alt)
		{
			auto click = ImGui::IsMouseClicked(0);
			auto doubleClick = ImGui::IsMouseDoubleClicked(0);
			auto t = ImGui::GetTime();
			auto tripleClick = click && !doubleClick && (mLastClick != -1.0f && (t - mLastClick) < io.MouseDoubleClickTime);

			/*
				Left mouse button triple click
			*/

			if (tripleClick)
			{
				if (!ctrl)
				{
					mState.mCursorPosition = mInteractiveStart = mInteractiveEnd = SanitizeCoordinates(ScreenPosToCoordinates(ImGui::GetMousePos()));
					mSelectionMode = SelectionMode::Line;
					SetSelection(mInteractiveStart, mInteractiveEnd, mSelectionMode);
				}

				mLastClick = -1.0f;
			}

			/*
				Left mouse button double click
			*/

			else if (doubleClick)
			{
				if (!ctrl)
				{
					mState.mCursorPosition = mInteractiveStart = mInteractiveEnd = SanitizeCoordinates(ScreenPosToCoordinates(ImGui::GetMousePos()));
					if (mSelectionMode == SelectionMode::Line)
						mSelectionMode = SelectionMode::Normal;
					else
						mSelectionMode = SelectionMode::Word;
					SetSelection(mInteractiveStart, mInteractiveEnd, mSelectionMode);
				}

				mLastClick = (float)ImGui::GetTime();
			}

			/*
				Left mouse button click
			*/
			else if (click)
			{
				mState.mCursorPosition = mInteractiveStart = mInteractiveEnd = SanitizeCoordinates(ScreenPosToCoordinates(ImGui::GetMousePos()));
				if (ctrl)
					mSelectionMode = SelectionMode::Word;
				else
					mSelectionMode = SelectionMode::Normal;
				SetSelection(mInteractiveStart, mInteractiveEnd, mSelectionMode);

				mLastClick = (float)ImGui::GetTime();
			}
			// Mouse left button dragging (=> update selection)
			else if (ImGui::IsMouseDragging(0) && ImGui::IsMouseDown(0))
			{
				io.WantCaptureMouse = true;
				mState.mCursorPosition = mInteractiveEnd = SanitizeCoordinates(ScreenPosToCoordinates(ImGui::GetMousePos()));
				SetSelection(mInteractiveStart, mInteractiveEnd, mSelectionMode);
			}
		}
	}
}

std::string wstrtostr(const std::wstring& wstr)
{
	// Convert a Unicode string to an ASCII string
	std::string strTo;
	size_t sizeRequired = WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, NULL, 0, NULL, NULL);
	char* szTo = new char[sizeRequired + 1];
	szTo[wstr.size()] = L'\0';
	WideCharToMultiByte(CP_UTF8, 0, wstr.c_str(), -1, szTo, (int)sizeRequired, NULL, NULL);
	strTo = szTo;
	delete[] szTo;
	return strTo;
}




void TextEditor::Render()
{
	std::string b;
	/* Compute mCharAdvance regarding to scaled font size (Ctrl + mouse wheel)*/
	const float fontSize = ImGui::GetFont()->CalcTextSizeA(ImGui::GetFontSize(), FLT_MAX, -1.0f, (char*)"#", nullptr, nullptr).x;
	mCharAdvance = ImVec2(fontSize, ImGui::GetTextLineHeightWithSpacing() * mLineSpacing);

	/* Update palette with the current alpha from style */
	for (int i = 0; i < (int)PaletteIndex::Max; ++i)
	{
		auto color = ImGui::ColorConvertU32ToFloat4(mPaletteBase[i]);
		color.w *= ImGui::GetStyle().Alpha;
		mPalette[i] = ImGui::ColorConvertFloat4ToU32(color);
	}
	
	static std::wstring buffer;
	assert(buffer.empty());
	
	auto contentSize = ImGui::GetWindowContentRegionMax();
	auto drawList = ImGui::GetWindowDrawList();
	float longest(mTextStart);
	
	if (mScrollToTop)
	{
		mScrollToTop = false;
		ImGui::SetScrollY(0.f);
	}

	ImVec2 cursorScreenPos = ImGui::GetCursorScreenPos();
	auto scrollX = ImGui::GetScrollX();
	auto scrollY = ImGui::GetScrollY();

	auto lineNo = (int)floor(scrollY / mCharAdvance.y);
	auto globalLineMax = (int)mLines.size();
	auto lineMax = std::max(0, std::min((int)mLines.size() - 1, lineNo + (int)floor((scrollY + contentSize.y) / mCharAdvance.y)));

	// Deduce mTextStart by evaluating mLines size (global lineMax) plus two spaces as text width
	Char buf[16];
	_snwprintf(buf, 16, L" %d ", globalLineMax);	
	b = wstrtostr(buf);
	mTextStart = ImGui::GetFont()->CalcTextSizeA(ImGui::GetFontSize(), FLT_MAX, -1.0f, (char*)b.c_str(), nullptr, nullptr).x + mLeftMargin;

	if (!mLines.empty())
	{
		auto fontScale = ImGui::GetFontSize() / ImGui::GetFont()->FontSize;
		float spaceSize = ImGui::GetFont()->CalcTextSizeA(ImGui::GetFontSize(), FLT_MAX, -1.0f, (char*)" ", nullptr, nullptr).x;

		while (lineNo <= lineMax)
		{
			ImVec2 lineStartScreenPos = ImVec2(cursorScreenPos.x, cursorScreenPos.y + lineNo * mCharAdvance.y);
			ImVec2 textScreenPos = ImVec2(lineStartScreenPos.x + mTextStart, lineStartScreenPos.y);

			auto& line = mLines[lineNo];
			longest = std::max(mTextStart + TextDistanceToLineStart(Coordinates(lineNo, (int)line.size())), longest);
			auto columnNo = 0;
			Coordinates lineStartCoord(lineNo, 0);
			Coordinates lineEndCoord(lineNo, (int)line.size());

			// Draw selection for the current line
			float sstart = -1.0f;
			float ssend = -1.0f;

			assert(mState.mSelectionStart <= mState.mSelectionEnd);
			if (mState.mSelectionStart <= lineEndCoord)
				sstart = mState.mSelectionStart > lineStartCoord ? TextDistanceToLineStart(mState.mSelectionStart) : 0.0f;
			if (mState.mSelectionEnd > lineStartCoord)
				ssend = TextDistanceToLineStart(mState.mSelectionEnd < lineEndCoord ? mState.mSelectionEnd : lineEndCoord);

			if (mState.mSelectionEnd.mLine > lineNo)
				ssend += mCharAdvance.x;

			if (sstart != -1 && ssend != -1 && sstart < ssend)
			{
				ImVec2 vstart(lineStartScreenPos.x + mTextStart + sstart, lineStartScreenPos.y);
				ImVec2 vend(lineStartScreenPos.x + mTextStart + ssend, lineStartScreenPos.y + mCharAdvance.y);
				drawList->AddRectFilled(vstart, vend, mPalette[(int)PaletteIndex::Selection]);
			}

			// Draw breakpoints
			auto start = ImVec2(lineStartScreenPos.x + scrollX, lineStartScreenPos.y);

			if (mBreakpoints.count(lineNo + 1) != 0)
			{
				auto end = ImVec2(lineStartScreenPos.x + contentSize.x + 2.0f * scrollX, lineStartScreenPos.y + mCharAdvance.y);
				drawList->AddRectFilled(start, end, mPalette[(int)PaletteIndex::Breakpoint]);
			}

			// Draw error markers
			auto errorIt = mErrorMarkers.find(lineNo + 1);
			if (errorIt != mErrorMarkers.end())
			{
				auto end = ImVec2(lineStartScreenPos.x + contentSize.x + 2.0f * scrollX, lineStartScreenPos.y + mCharAdvance.y);
				drawList->AddRectFilled(start, end, mPalette[(int)PaletteIndex::ErrorMarker]);

				if (ImGui::IsMouseHoveringRect(lineStartScreenPos, end))
				{
					ImGui::BeginTooltip();
					ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.2f, 0.2f, 1.0f));
					ImGui::Text("Error at line %d:", errorIt->first);
					ImGui::PopStyleColor();
					ImGui::Separator();
					ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 0.2f, 1.0f));
					ImGui::Text("%s", errorIt->second.c_str());
					ImGui::PopStyleColor();
					ImGui::EndTooltip();
				}
			}

			// Draw line number (right aligned)
			_snwprintf(buf, 16, L"%d  ", lineNo + 1);
			b = wstrtostr(buf);
			auto lineNoWidth = ImGui::GetFont()->CalcTextSizeA(ImGui::GetFontSize(), FLT_MAX, -1.0f, (char*)b.c_str(), nullptr, nullptr).x;
			drawList->AddText(ImVec2(lineStartScreenPos.x + mTextStart - lineNoWidth, lineStartScreenPos.y), mPalette[(int)PaletteIndex::LineNumber], (char*)buf);

			// Highlight the current line (where the cursor is)
			if (mState.mCursorPosition.mLine == lineNo)
			{
				auto focused = ImGui::IsWindowFocused();

				if (!HasSelection())
				{
					auto end = ImVec2(start.x + contentSize.x + scrollX, start.y + mCharAdvance.y);
					drawList->AddRectFilled(start, end, mPalette[(int)(focused ? PaletteIndex::CurrentLineFill : PaletteIndex::CurrentLineFillInactive)]);
					drawList->AddRect(start, end, mPalette[(int)PaletteIndex::CurrentLineEdge], 1.0f);
				}

				float cx = TextDistanceToLineStart(mState.mCursorPosition);

				if (focused)
				{
					static auto timeStart = std::chrono::system_clock::now();
					auto timeEnd = std::chrono::system_clock::now();
					auto diff = timeEnd - timeStart;
					auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(diff).count();
					if (elapsed > 400)
					{
						ImVec2 cstart(textScreenPos.x + cx, lineStartScreenPos.y);
						ImVec2 cend(textScreenPos.x + cx + (mOverwrite ? mCharAdvance.x : 1.0f), lineStartScreenPos.y + mCharAdvance.y);
						drawList->AddRectFilled(cstart, cend, mPalette[(int)PaletteIndex::Cursor]);
						if (elapsed > 800)
							timeStart = timeEnd;
					}
				}
			}

			// Render colorized text
			auto prevColor = line.empty() ? mPalette[(int)PaletteIndex::Default] : GetGlyphColor(line[0]);
			ImVec2 bufferOffset;

			for (auto& glyph : line)
			{
				auto color = GetGlyphColor(glyph);

				if ((color != prevColor || glyph.mChar == L'\t') && !buffer.empty())
				{
					const ImVec2 newOffset(textScreenPos.x + bufferOffset.x, textScreenPos.y + bufferOffset.y);
					b = wstrtostr(buffer);
					drawList->AddText(newOffset, prevColor, (char*)b.c_str());
					
					auto textSize = ImGui::GetFont()->CalcTextSizeA(ImGui::GetFontSize(), FLT_MAX, -1.0f, (char*)b.c_str(), nullptr, nullptr);
					bufferOffset.x += textSize.x;
					buffer.clear();
				}
				prevColor = color;

				if (glyph.mChar == L'\t')
					bufferOffset.x = (1.0f * fontScale + std::floor((1.0f + bufferOffset.x)) / (float(mTabSize) * spaceSize)) * (float(mTabSize) * spaceSize);
				else
					AppendBuffer(buffer, glyph.mChar, 0);
				++columnNo;
			}
			size_t sizeRequired = 0;
			if (!buffer.empty())
			{
				//std::wcout << buffer << std::endl;
				sizeRequired = WideCharToMultiByte(CP_UTF8, 0, buffer.c_str(), -1, NULL, 0, NULL, NULL);
				const ImVec2 newOffset(textScreenPos.x + bufferOffset.x, textScreenPos.y + bufferOffset.y);								
				b=wstrtostr(buffer);								
				drawList->AddText(newOffset, prevColor, (char*)b.c_str());
				buffer.clear();
			}
			
			lineNo++;
			
		}

		// Draw a tooltip on known identifiers/preprocessor symbols
		if (ImGui::IsMousePosValid())
		{
			auto id = GetWordAt(ScreenPosToCoordinates(ImGui::GetMousePos()));
			if (!id.empty())
			{
				auto it = mLanguageDefinition.mIdentifiers.find(id);
				if (it != mLanguageDefinition.mIdentifiers.end())
				{
					ImGui::BeginTooltip();
					ImGui::TextUnformatted((char*)it->second.mDeclaration.c_str());
					ImGui::EndTooltip();
				}
				else
				{
					auto pi = mLanguageDefinition.mPreprocIdentifiers.find(id);
					if (pi != mLanguageDefinition.mPreprocIdentifiers.end())
					{
						ImGui::BeginTooltip();
						ImGui::TextUnformatted((char*)pi->second.mDeclaration.c_str());
						ImGui::EndTooltip();
					}
				}
			}
		}
	}


	ImGui::Dummy(ImVec2((longest + 2), mLines.size() * mCharAdvance.y));

	if (mScrollToCursor)
	{
		EnsureCursorVisible();
		ImGui::SetWindowFocus();
		mScrollToCursor = false;
	}
}

void TextEditor::Render(const Char* aTitle, const ImVec2& aSize, bool aBorder)
{
	mWithinRender = true;
	mTextChanged = false;
	mCursorPositionChanged = false;

	ImGui::PushStyleColor(ImGuiCol_ChildWindowBg, ImGui::ColorConvertU32ToFloat4(mPalette[(int)PaletteIndex::Background]));
	ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 0.0f));
	ImGui::BeginChild((char*)aTitle, aSize, aBorder, ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_AlwaysHorizontalScrollbar | ImGuiWindowFlags_NoMove);
	ImGui::PushAllowKeyboardFocus(true);

	HandleKeyboardInputs();
	HandleMouseInputs();
	ColorizeInternal();
	Render();

	ImGui::PopAllowKeyboardFocus();
	ImGui::EndChild();
	ImGui::PopStyleVar();
	ImGui::PopStyleColor();

	mWithinRender = false;
}

void TextEditor::SetText(const std::wstring & aText)
{
	mLines.clear();
	mLines.emplace_back(Line());
	for (auto chr : aText)
	{
		if (chr == L'\r')
		{
			// ignore the carriage return character
		}
		else if (chr == L'\n')
			mLines.emplace_back(Line());
		else
		{
			mLines.back().emplace_back(Glyph(chr, PaletteIndex::Default));
		}
	}
	
	mTextChanged = true;
	mScrollToTop = true;

	mUndoBuffer.clear();
	mUndoIndex = 0;

	Colorize();
}

void TextEditor::SetTextLines(const std::vector<std::wstring> & aLines)
{
	mLines.clear();

	if (aLines.empty())
	{
		mLines.emplace_back(Line());
	}
	else
	{
		mLines.resize(aLines.size());

		for (size_t i = 0; i < aLines.size(); ++i)
		{
			const std::wstring & aLine = aLines[i];

			mLines[i].reserve(aLine.size());
			for (size_t j = 0; j < aLine.size(); ++j)
				mLines[i].emplace_back(Glyph(aLine[j], PaletteIndex::Default));
		}
	}

	mTextChanged = true;
	mScrollToTop = true;

	mUndoBuffer.clear();
	mUndoIndex = 0;

	Colorize();
}

void TextEditor::EnterCharacter(Char aChar, bool aShift)
{
	assert(!mReadOnly);

	UndoRecord u;

	u.mBefore = mState;

	if (HasSelection())
	{
		if (aChar == L'\t')
		{
			auto start = mState.mSelectionStart;
			auto end = mState.mSelectionEnd;

			if (start > end)
				std::swap(start, end);
			start.mColumn = 0;
			//			end.mColumn = end.mLine < mLines.size() ? mLines[end.mLine].size() : 0;
			if (end.mColumn == 0 && end.mLine > 0)
			{
				--end.mLine;
				end.mColumn = (int)mLines[end.mLine].size();
			}

			u.mRemovedStart = start;
			u.mRemovedEnd = end;
			u.mRemoved = GetText(start, end);

			bool modified = false;

			for (int i = start.mLine; i <= end.mLine; i++)
			{
				auto& line = mLines[i];
				if (aShift)
				{
					if (line.empty() == false)
					{
						if (line.front().mChar == L'\t')
						{
							line.erase(line.begin());
							if (i == end.mLine && end.mColumn > 0)
								end.mColumn--;
							modified = true;
						}
					}
					else
					{
						for (int j = 0; j < mTabSize && line.empty() == false && line.front().mChar == L' '; j++)
						{
							line.erase(line.begin());
							if (i == end.mLine && end.mColumn > 0)
								end.mColumn--;
							modified = true;
						}
					}
				}
				else
				{
					line.insert(line.begin(), Glyph(L'\t', TextEditor::PaletteIndex::Background));
					if (i == end.mLine)
						++end.mColumn;
					modified = true;					
				}
			}

			if (modified)
			{
				u.mAddedStart = start;
				u.mAddedEnd = end;
				u.mAdded = GetText(start, end);

				mTextChanged = true;

				AddUndo(u);
				EnsureCursorVisible();

				
			}

			return;
		}
		else
		{
			u.mRemoved = GetSelectedText();
			u.mRemovedStart = mState.mSelectionStart;
			u.mRemovedEnd = mState.mSelectionEnd;
			DeleteSelection();
		}
	}

	auto coord = GetActualCursorCoordinates();
	u.mAddedStart = coord;

	assert(!mLines.empty());

	if (aChar == L'\n')
	{
		InsertLine(coord.mLine + 1);
		auto& line = mLines[coord.mLine];
		auto& newLine = mLines[coord.mLine + 1];

		if (mLanguageDefinition.mAutoIndentation)
		{
			for (size_t it = 0; it < line.size() && isblank(line[it].mChar); ++it)
				newLine.push_back(line[it]);
		}

		const size_t whitespaceSize = newLine.size();
		newLine.insert(newLine.end(), line.begin() + coord.mColumn, line.end());
		line.erase(line.begin() + coord.mColumn, line.begin() + line.size());
		SetCursorPosition(Coordinates(coord.mLine + 1, (int)whitespaceSize));
	}
	else
	{
		auto& line = mLines[coord.mLine];
		if (mOverwrite && (int)line.size() > coord.mColumn)
			line[coord.mColumn] = Glyph(aChar, PaletteIndex::Default);
		else
			line.insert(line.begin() + coord.mColumn, Glyph(aChar, PaletteIndex::Default));
		SetCursorPosition(Coordinates(coord.mLine, coord.mColumn + 1));
	}

	mTextChanged = true;

	u.mAdded = aChar;
	u.mAddedEnd = GetActualCursorCoordinates();
	u.mAfter = mState;

	AddUndo(u);

	Colorize(coord.mLine - 1, 3);
	EnsureCursorVisible();
}

void TextEditor::SetReadOnly(bool aValue)
{
	mReadOnly = aValue;
}

void TextEditor::SetCursorPosition(const Coordinates & aPosition)
{
	if (mState.mCursorPosition != aPosition)
	{
		mState.mCursorPosition = aPosition;
		mCursorPositionChanged = true;
		EnsureCursorVisible();
	}
}

void TextEditor::SetSelectionStart(const Coordinates & aPosition)
{
	mState.mSelectionStart = SanitizeCoordinates(aPosition);
	if (mState.mSelectionStart > mState.mSelectionEnd)
		std::swap(mState.mSelectionStart, mState.mSelectionEnd);
}

void TextEditor::SetSelectionEnd(const Coordinates & aPosition)
{
	mState.mSelectionEnd = SanitizeCoordinates(aPosition);
	if (mState.mSelectionStart > mState.mSelectionEnd)
		std::swap(mState.mSelectionStart, mState.mSelectionEnd);
}

void TextEditor::SetSelection(const Coordinates & aStart, const Coordinates & aEnd, SelectionMode aMode)
{
	auto oldSelStart = mState.mSelectionStart;
	auto oldSelEnd = mState.mSelectionEnd;

	mState.mSelectionStart = SanitizeCoordinates(aStart);
	mState.mSelectionEnd = SanitizeCoordinates(aEnd);
	if (aStart > aEnd)
		std::swap(mState.mSelectionStart, mState.mSelectionEnd);

	switch (aMode)
	{
	case TextEditor::SelectionMode::Normal:
		break;
	case TextEditor::SelectionMode::Word:
	{
		mState.mSelectionStart = FindWordStart(mState.mSelectionStart);
		if (!IsOnWordBoundary(mState.mSelectionEnd))
			mState.mSelectionEnd = FindWordEnd(FindWordStart(mState.mSelectionEnd));
		break;
	}
	case TextEditor::SelectionMode::Line:
	{
		const auto lineNo = mState.mSelectionEnd.mLine;
		const auto lineSize = (size_t)lineNo < mLines.size() ? mLines[lineNo].size() : 0;
		mState.mSelectionStart = Coordinates(mState.mSelectionStart.mLine, 0);
		mState.mSelectionEnd = Coordinates(lineNo, (int)lineSize);
		break;
	}
	default:
		break;
	}

	if (mState.mSelectionStart != oldSelStart ||
		mState.mSelectionEnd != oldSelEnd)
		mCursorPositionChanged = true;
}

void TextEditor::InsertText(const std::wstring & aValue)
{
	InsertText(aValue.c_str());
}

void TextEditor::InsertText(const Char * aValue)
{
	if (aValue == nullptr)
		return;

	auto pos = GetActualCursorCoordinates();
	auto start = std::min(pos, mState.mSelectionStart);
	int totalLines = pos.mLine - start.mLine;

	totalLines += InsertTextAt(pos, aValue);

	SetSelection(pos, pos);
	SetCursorPosition(pos);
	Colorize(start.mLine - 1, totalLines + 2);
}

void TextEditor::DeleteSelection()
{
	assert(mState.mSelectionEnd >= mState.mSelectionStart);

	if (mState.mSelectionEnd == mState.mSelectionStart)
		return;

	DeleteRange(mState.mSelectionStart, mState.mSelectionEnd);

	SetSelection(mState.mSelectionStart, mState.mSelectionStart);
	SetCursorPosition(mState.mSelectionStart);
	Colorize(mState.mSelectionStart.mLine, 1);
}

void TextEditor::MoveUp(int aAmount, bool aSelect)
{
	auto oldPos = mState.mCursorPosition;
	mState.mCursorPosition.mLine = std::max(0, mState.mCursorPosition.mLine - aAmount);
	if (oldPos != mState.mCursorPosition)
	{
		if (aSelect)
		{
			if (oldPos == mInteractiveStart)
				mInteractiveStart = mState.mCursorPosition;
			else if (oldPos == mInteractiveEnd)
				mInteractiveEnd = mState.mCursorPosition;
			else
			{
				mInteractiveStart = mState.mCursorPosition;
				mInteractiveEnd = oldPos;
			}
		}
		else
			mInteractiveStart = mInteractiveEnd = mState.mCursorPosition;
		SetSelection(mInteractiveStart, mInteractiveEnd);

		EnsureCursorVisible();
	}
}

void TextEditor::MoveDown(int aAmount, bool aSelect)
{
	assert(mState.mCursorPosition.mColumn >= 0);
	auto oldPos = mState.mCursorPosition;
	mState.mCursorPosition.mLine = std::max(0, std::min((int)mLines.size() - 1, mState.mCursorPosition.mLine + aAmount));

	if (mState.mCursorPosition != oldPos)
	{
		if (aSelect)
		{
			if (oldPos == mInteractiveEnd)
				mInteractiveEnd = mState.mCursorPosition;
			else if (oldPos == mInteractiveStart)
				mInteractiveStart = mState.mCursorPosition;
			else
			{
				mInteractiveStart = oldPos;
				mInteractiveEnd = mState.mCursorPosition;
			}
		}
		else
			mInteractiveStart = mInteractiveEnd = mState.mCursorPosition;
		SetSelection(mInteractiveStart, mInteractiveEnd);

		EnsureCursorVisible();
	}
}

void TextEditor::MoveLeft(int aAmount, bool aSelect, bool aWordMode)
{
	if (mLines.empty())
		return;

	auto oldPos = mState.mCursorPosition;
	mState.mCursorPosition = GetActualCursorCoordinates();

	while (aAmount-- > 0)
	{
		if (mState.mCursorPosition.mColumn == 0)
		{
			if (mState.mCursorPosition.mLine > 0)
			{
				--mState.mCursorPosition.mLine;
				mState.mCursorPosition.mColumn = (int)mLines[mState.mCursorPosition.mLine].size();
			}
		}
		else
		{
			mState.mCursorPosition.mColumn = std::max(0, mState.mCursorPosition.mColumn - 1);
			if (aWordMode)
				mState.mCursorPosition = FindWordStart(mState.mCursorPosition);
		}
	}

	assert(mState.mCursorPosition.mColumn >= 0);
	if (aSelect)
	{
		if (oldPos == mInteractiveStart)
			mInteractiveStart = mState.mCursorPosition;
		else if (oldPos == mInteractiveEnd)
			mInteractiveEnd = mState.mCursorPosition;
		else
		{
			mInteractiveStart = mState.mCursorPosition;
			mInteractiveEnd = oldPos;
		}
	}
	else
		mInteractiveStart = mInteractiveEnd = mState.mCursorPosition;
	SetSelection(mInteractiveStart, mInteractiveEnd, aSelect && aWordMode ? SelectionMode::Word : SelectionMode::Normal);

	EnsureCursorVisible();
}

void TextEditor::MoveRight(int aAmount, bool aSelect, bool aWordMode)
{
	auto oldPos = mState.mCursorPosition;

	if (mLines.empty())
		return;

	while (aAmount-- > 0)
	{
		auto& line = mLines[mState.mCursorPosition.mLine];
		if (mState.mCursorPosition.mColumn >= (int)line.size())
		{
			if (mState.mCursorPosition.mLine < (int)mLines.size() - 1)
			{
				mState.mCursorPosition.mLine = std::max(0, std::min((int)mLines.size() - 1, mState.mCursorPosition.mLine + 1));
				mState.mCursorPosition.mColumn = 0;
			}
		}
		else
		{
			mState.mCursorPosition.mColumn = std::max(0, std::min((int)line.size(), mState.mCursorPosition.mColumn + 1));
			if (aWordMode)
				mState.mCursorPosition = FindWordEnd(mState.mCursorPosition);
		}
	}

	if (aSelect)
	{
		if (oldPos == mInteractiveEnd)
			mInteractiveEnd = SanitizeCoordinates(mState.mCursorPosition);
		else if (oldPos == mInteractiveStart)
			mInteractiveStart = mState.mCursorPosition;
		else
		{
			mInteractiveStart = oldPos;
			mInteractiveEnd = mState.mCursorPosition;
		}
	}
	else
		mInteractiveStart = mInteractiveEnd = mState.mCursorPosition;
	SetSelection(mInteractiveStart, mInteractiveEnd, aSelect && aWordMode ? SelectionMode::Word : SelectionMode::Normal);

	EnsureCursorVisible();
}

void TextEditor::MoveTop(bool aSelect)
{
	auto oldPos = mState.mCursorPosition;
	SetCursorPosition(Coordinates(0, 0));

	if (mState.mCursorPosition != oldPos)
	{
		if (aSelect)
		{
			mInteractiveEnd = oldPos;
			mInteractiveStart = mState.mCursorPosition;
		}
		else
			mInteractiveStart = mInteractiveEnd = mState.mCursorPosition;
		SetSelection(mInteractiveStart, mInteractiveEnd);
	}
}

void TextEditor::TextEditor::MoveBottom(bool aSelect)
{
	auto oldPos = GetCursorPosition();
	auto newPos = Coordinates((int)mLines.size() - 1, 0);
	SetCursorPosition(newPos);
	if (aSelect)
	{
		mInteractiveStart = oldPos;
		mInteractiveEnd = newPos;
	}
	else
		mInteractiveStart = mInteractiveEnd = newPos;
	SetSelection(mInteractiveStart, mInteractiveEnd);
}

void TextEditor::MoveHome(bool aSelect)
{
	auto oldPos = mState.mCursorPosition;
	SetCursorPosition(Coordinates(mState.mCursorPosition.mLine, 0));

	if (mState.mCursorPosition != oldPos)
	{
		if (aSelect)
		{
			if (oldPos == mInteractiveStart)
				mInteractiveStart = mState.mCursorPosition;
			else if (oldPos == mInteractiveEnd)
				mInteractiveEnd = mState.mCursorPosition;
			else
			{
				mInteractiveStart = mState.mCursorPosition;
				mInteractiveEnd = oldPos;
			}
		}
		else
			mInteractiveStart = mInteractiveEnd = mState.mCursorPosition;
		SetSelection(mInteractiveStart, mInteractiveEnd);
	}
}

void TextEditor::MoveEnd(bool aSelect)
{
	auto oldPos = mState.mCursorPosition;
	SetCursorPosition(Coordinates(mState.mCursorPosition.mLine, (int)mLines[oldPos.mLine].size()));

	if (mState.mCursorPosition != oldPos)
	{
		if (aSelect)
		{
			if (oldPos == mInteractiveEnd)
				mInteractiveEnd = mState.mCursorPosition;
			else if (oldPos == mInteractiveStart)
				mInteractiveStart = mState.mCursorPosition;
			else
			{
				mInteractiveStart = oldPos;
				mInteractiveEnd = mState.mCursorPosition;
			}
		}
		else
			mInteractiveStart = mInteractiveEnd = mState.mCursorPosition;
		SetSelection(mInteractiveStart, mInteractiveEnd);
	}
}

void TextEditor::Delete()
{
	assert(!mReadOnly);

	if (mLines.empty())
		return;

	UndoRecord u;
	u.mBefore = mState;

	if (HasSelection())
	{
		u.mRemoved = GetSelectedText();
		u.mRemovedStart = mState.mSelectionStart;
		u.mRemovedEnd = mState.mSelectionEnd;

		DeleteSelection();
	}
	else
	{
		auto pos = GetActualCursorCoordinates();
		SetCursorPosition(pos);
		auto& line = mLines[pos.mLine];

		if (pos.mColumn == (int)line.size())
		{
			if (pos.mLine == (int)mLines.size() - 1)
				return;

			u.mRemoved = L'\n';
			u.mRemovedStart = u.mRemovedEnd = GetActualCursorCoordinates();
			Advance(u.mRemovedEnd);

			auto& nextLine = mLines[pos.mLine + 1];
			line.insert(line.end(), nextLine.begin(), nextLine.end());
			RemoveLine(pos.mLine + 1);
		}
		else
		{
			u.mRemoved = line[pos.mColumn].mChar;
			u.mRemovedStart = u.mRemovedEnd = GetActualCursorCoordinates();
			u.mRemovedEnd.mColumn++;

			line.erase(line.begin() + pos.mColumn);
		}

		mTextChanged = true;

		Colorize(pos.mLine, 1);
	}

	u.mAfter = mState;
	AddUndo(u);
}

void TextEditor::BackSpace()
{
	assert(!mReadOnly);

	if (mLines.empty())
		return;

	UndoRecord u;
	u.mBefore = mState;

	if (HasSelection())
	{
		u.mRemoved = GetSelectedText();
		u.mRemovedStart = mState.mSelectionStart;
		u.mRemovedEnd = mState.mSelectionEnd;

		DeleteSelection();
	}
	else
	{
		auto pos = GetActualCursorCoordinates();
		SetCursorPosition(pos);

		if (mState.mCursorPosition.mColumn == 0)
		{
			if (mState.mCursorPosition.mLine == 0)
				return;

			u.mRemoved = L'\n';
			u.mRemovedStart = u.mRemovedEnd = Coordinates(pos.mLine - 1, (int)mLines[pos.mLine - 1].size());
			Advance(u.mRemovedEnd);

			auto& line = mLines[mState.mCursorPosition.mLine];
			auto& prevLine = mLines[mState.mCursorPosition.mLine - 1];
			auto prevSize = (int)prevLine.size();
			prevLine.insert(prevLine.end(), line.begin(), line.end());

			ErrorMarkers etmp;
			for (auto& i : mErrorMarkers)
				etmp.insert(ErrorMarkers::value_type(i.first - 1 == mState.mCursorPosition.mLine ? i.first - 1 : i.first, i.second));
			mErrorMarkers = std::move(etmp);

			RemoveLine(mState.mCursorPosition.mLine);
			--mState.mCursorPosition.mLine;
			mState.mCursorPosition.mColumn = prevSize;
		}
		else
		{
			auto& line = mLines[mState.mCursorPosition.mLine];

			u.mRemoved = line[pos.mColumn - 1].mChar;
			u.mRemovedStart = u.mRemovedEnd = GetActualCursorCoordinates();
			--u.mRemovedStart.mColumn;

			--mState.mCursorPosition.mColumn;
			if (mState.mCursorPosition.mColumn < (int)line.size())
				line.erase(line.begin() + mState.mCursorPosition.mColumn);
		}

		mTextChanged = true;

		EnsureCursorVisible();
		Colorize(mState.mCursorPosition.mLine, 1);
	}

	u.mAfter = mState;
	AddUndo(u);
}

void TextEditor::SelectWordUnderCursor()
{
	auto c = GetCursorPosition();
	SetSelection(FindWordStart(c), FindWordEnd(c));
}

void TextEditor::SelectAll()
{
	SetSelection(Coordinates(0, 0), Coordinates((int)mLines.size(), 0));
}

bool TextEditor::HasSelection() const
{
	return mState.mSelectionEnd > mState.mSelectionStart;
}

void TextEditor::Copy()
{
	if (HasSelection())
	{
		ImGui::SetClipboardText((char*)GetSelectedText().c_str());
	}
	else
	{
		if (!mLines.empty())
		{
			std::wstring str;
			auto& line = mLines[GetActualCursorCoordinates().mLine];
			for (auto& g : line)
				str.push_back(g.mChar);
			ImGui::SetClipboardText((char*)str.c_str());
		}
	}
}

void TextEditor::Cut()
{
	if (IsReadOnly())
	{
		Copy();
	}
	else
	{
		if (HasSelection())
		{
			UndoRecord u;
			u.mBefore = mState;
			u.mRemoved = GetSelectedText();
			u.mRemovedStart = mState.mSelectionStart;
			u.mRemovedEnd = mState.mSelectionEnd;

			Copy();
			DeleteSelection();

			u.mAfter = mState;
			AddUndo(u);
		}
	}
}

void TextEditor::Paste()
{
	auto clipText = ImGui::GetClipboardText();
	if (clipText != nullptr && strlen(clipText) > 0)
	{
		UndoRecord u;
		u.mBefore = mState;

		if (HasSelection())
		{
			u.mRemoved = GetSelectedText();
			u.mRemovedStart = mState.mSelectionStart;
			u.mRemovedEnd = mState.mSelectionEnd;
			DeleteSelection();
		}

		u.mAdded = (Char*)clipText;
		u.mAddedStart = GetActualCursorCoordinates();

		InsertText((Char*)clipText);

		u.mAddedEnd = GetActualCursorCoordinates();
		u.mAfter = mState;
		AddUndo(u);
	}
}

bool TextEditor::CanUndo() const
{
	return mUndoIndex > 0;
}

bool TextEditor::CanRedo() const
{
	return mUndoIndex < (int)mUndoBuffer.size();
}

void TextEditor::Undo(int aSteps)
{
	while (CanUndo() && aSteps-- > 0)
		mUndoBuffer[--mUndoIndex].Undo(this);
}

void TextEditor::Redo(int aSteps)
{
	while (CanRedo() && aSteps-- > 0)
		mUndoBuffer[mUndoIndex++].Redo(this);
}

const TextEditor::Palette & TextEditor::GetDarkPalette()
{
	const static Palette p = { {
		0xff7f7f7f,	// Default
		0xffd69c56,	// Keyword	
		0xff00ff00,	// Number
		0xff7070e0,	// String
		0xff70a0e0, // Char literal
		0xffffffff, // Punctuation
		0xff408080,	// Preprocessor
		0xffaaaaaa, // Identifier
		0xff9bc64d, // Known identifier
		0xffc040a0, // Preproc identifier
		0xff206020, // Comment (single line)
		0xff406020, // Comment (multi line)
		0xff101010, // Background
		0xffe0e0e0, // Cursor
		0x80a06020, // Selection
		0x800020ff, // ErrorMarker
		0x40f08000, // Breakpoint
		0xff707000, // Line number
		0x40000000, // Current line fill
		0x40808080, // Current line fill (inactive)
		0x40a0a0a0, // Current line edge
	} };
	return p;
}

const TextEditor::Palette & TextEditor::GetLightPalette()
{
	const static Palette p = { {
		0xff7f7f7f,	// None
		0xffff0c06,	// Keyword	
		0xff008000,	// Number
		0xff2020a0,	// String
		0xff304070, // Char literal
		0xff000000, // Punctuation
		0xff406060,	// Preprocessor
		0xff404040, // Identifier
		0xff606010, // Known identifier
		0xffc040a0, // Preproc identifier
		0xff205020, // Comment (single line)
		0xff405020, // Comment (multi line)
		0xffffffff, // Background
		0xff000000, // Cursor
		0x80600000, // Selection
		0xa00010ff, // ErrorMarker
		0x80f08000, // Breakpoint
		0xff505000, // Line number
		0x40000000, // Current line fill
		0x40808080, // Current line fill (inactive)
		0x40000000, // Current line edge
	} };
	return p;
}

const TextEditor::Palette & TextEditor::GetRetroBluePalette()
{
	const static Palette p = { {
		0xff00ffff,	// None
		0xffffff00,	// Keyword	
		0xff00ff00,	// Number
		0xff808000,	// String
		0xff808000, // Char literal
		0xffffffff, // Punctuation
		0xff008000,	// Preprocessor
		0xff00ffff, // Identifier
		0xffffffff, // Known identifier
		0xffff00ff, // Preproc identifier
		0xff808080, // Comment (single line)
		0xff404040, // Comment (multi line)
		0xff800000, // Background
		0xff0080ff, // Cursor
		0x80ffff00, // Selection
		0xa00000ff, // ErrorMarker
		0x80ff8000, // Breakpoint
		0xff808000, // Line number
		0x40000000, // Current line fill
		0x40808080, // Current line fill (inactive)
		0x40000000, // Current line edge
	} };
	return p;
}


std::wstring TextEditor::GetText() const
{
	return GetText(Coordinates(), Coordinates((int)mLines.size(), 0));
}

std::vector<std::wstring> TextEditor::GetTextLines() const
{
	std::vector<std::wstring> result;

	result.reserve(mLines.size());

	for (auto & line : mLines)
	{
		std::wstring text;

		text.resize(line.size());

		for (size_t i = 0; i < line.size(); ++i)
			text[i] = line[i].mChar;

		result.emplace_back(std::move(text));
	}

	return result;
}

std::wstring TextEditor::GetSelectedText() const
{
	return GetText(mState.mSelectionStart, mState.mSelectionEnd);
}

std::wstring TextEditor::GetCurrentLineText()const
{
	auto lineLength = (int)mLines[mState.mCursorPosition.mLine].size();
	return GetText(Coordinates(mState.mCursorPosition.mLine, 0), Coordinates(mState.mCursorPosition.mLine, lineLength));
}

void TextEditor::ProcessInputs()
{
}

void TextEditor::Colorize(int aFromLine, int aLines)
{
	int toLine = aLines == -1 ? (int)mLines.size() : std::min((int)mLines.size(), aFromLine + aLines);
	mColorRangeMin = std::min(mColorRangeMin, aFromLine);
	mColorRangeMax = std::max(mColorRangeMax, toLine);
	mColorRangeMin = std::max(0, mColorRangeMin);
	mColorRangeMax = std::max(mColorRangeMin, mColorRangeMax);
	mCheckComments = true;
}

void TextEditor::ColorizeRange(int aFromLine, int aToLine)
{
	if (mLines.empty() || aFromLine >= aToLine)
		return;

	std::wstring buffer;
	std::wcmatch results;
	std::wstring id;

	int endLine = std::max(0, std::min((int)mLines.size(), aToLine));
	for (int i = aFromLine; i < endLine; ++i)
	{
		auto& line = mLines[i];

		if (line.empty())
			continue;

		buffer.resize(line.size());
		for (size_t j = 0; j < line.size(); ++j)
		{
			auto& col = line[j];
			buffer[j] = col.mChar;
			col.mColorIndex = PaletteIndex::Default;
		}

		const Char * bufferBegin = &buffer.front();
		const Char * bufferEnd = bufferBegin + buffer.size();

		auto last = bufferEnd;

		for (auto first = bufferBegin; first != last; )
		{
			const Char * token_begin = nullptr;
			const Char * token_end = nullptr;
			PaletteIndex token_color = PaletteIndex::Default;

			bool hasTokenizeResult = false;

			if (mLanguageDefinition.mTokenize != nullptr)
			{
				if (mLanguageDefinition.mTokenize(first, last, token_begin, token_end, token_color))
					hasTokenizeResult = true;
			}

			if (hasTokenizeResult == false)
			{
				// todo : remove
					//printf("using regex for %.*s\n", first + 10 < last ? 10 : int(last - first), first);

				for (auto& p : mRegexList)
				{
					if (std::regex_search(first, last, results, p.first, std::regex_constants::match_continuous))
					{
						hasTokenizeResult = true;

						auto& v = *results.begin();
						token_begin = v.first;
						token_end = v.second;
						token_color = p.second;
						break;
					}
				}
			}

			if (hasTokenizeResult == false)
			{
				first++;
			}
			else
			{
				const size_t token_length = token_end - token_begin;

				if (token_color == PaletteIndex::Identifier)
				{
					id.assign(token_begin, token_end);

					// todo : allmost all language definitions use lower case to specify keywords, so shouldn't this use ::tolower ?
					if (!mLanguageDefinition.mCaseSensitive)
						std::transform(id.begin(), id.end(), id.begin(), ::toupper);

					if (!line[first - bufferBegin].mPreprocessor)
					{
						if (mLanguageDefinition.mKeywords.count(id) != 0)
							token_color = PaletteIndex::Keyword;
						else if (mLanguageDefinition.mIdentifiers.count(id) != 0)
							token_color = PaletteIndex::KnownIdentifier;
						else if (mLanguageDefinition.mPreprocIdentifiers.count(id) != 0)
							token_color = PaletteIndex::PreprocIdentifier;
					}
					else
					{
						if (mLanguageDefinition.mPreprocIdentifiers.count(id) != 0)
							token_color = PaletteIndex::PreprocIdentifier;
					}
				}

				for (size_t j = 0; j < token_length; ++j)
					line[(token_begin - bufferBegin) + j].mColorIndex = token_color;

				first = token_end;
			}
		}
	}
}

void TextEditor::ColorizeInternal()
{
	if (mLines.empty())
		return;

	if (mCheckComments)
	{
		auto end = Coordinates((int)mLines.size(), 0);
		auto commentStart = end;
		auto withinString = false;
		auto withinSingleLineComment = false;
		auto withinPreproc = false;
		auto firstChar = true;			// there is no other non-whitespace characters in the line before
		auto concatenate = false;		// '\' on the very end of the line

		for (auto currentCoord = Coordinates(0, 0); currentCoord < end; Advance(currentCoord))
		{
			auto& line = mLines[currentCoord.mLine];

			if (currentCoord.mColumn == 0 && !concatenate)
			{
				withinSingleLineComment = false;
				withinPreproc = false;
				firstChar = true;
			}

			concatenate = false;

			if (!line.empty())
			{
				auto& g = line[currentCoord.mColumn];
				auto c = g.mChar;

				if (c != mLanguageDefinition.mPreprocChar && !isspace(c))
					firstChar = false;

				if (currentCoord.mColumn == line.size() - 1 && line[line.size() - 1].mChar == L'\\')
					concatenate = true;

				bool inComment = commentStart <= currentCoord;

				if (withinString)
				{
					line[currentCoord.mColumn].mMultiLineComment = inComment;

					if (c == L'\"')
					{
						if (currentCoord.mColumn + 1 < (int)line.size() && line[currentCoord.mColumn + 1].mChar == L'\"')
						{
							Advance(currentCoord);
							if (currentCoord.mColumn < (int)line.size())
								line[currentCoord.mColumn].mMultiLineComment = inComment;
						}
						else
							withinString = false;
					}
					else if (c == L'\\')
					{
						Advance(currentCoord);
						if (currentCoord.mColumn < (int)line.size())
							line[currentCoord.mColumn].mMultiLineComment = inComment;
					}
				}
				else
				{
					if (firstChar && c == mLanguageDefinition.mPreprocChar)
						withinPreproc = true;

					if (c == L'\"')
					{
						withinString = true;
						line[currentCoord.mColumn].mMultiLineComment = inComment;
					}
					else
					{
						auto pred = [](const Char& a, const Glyph& b) { return a == b.mChar; };
						auto from = line.begin() + currentCoord.mColumn;
						auto& startStr = mLanguageDefinition.mCommentStart;
						auto& singleStartStr = mLanguageDefinition.mSingleLineComment;
						if (singleStartStr.size() > 0 &&
							currentCoord.mColumn + singleStartStr.size() <= line.size() &&
							equals(singleStartStr.begin(), singleStartStr.end(), from, from + singleStartStr.size(), pred))
							withinSingleLineComment = true;
						else if (!withinSingleLineComment && currentCoord.mColumn + startStr.size() <= line.size() &&
							equals(startStr.begin(), startStr.end(), from, from + startStr.size(), pred))
							commentStart = currentCoord;

						inComment = commentStart <= currentCoord;

						line[currentCoord.mColumn].mMultiLineComment = inComment;
						line[currentCoord.mColumn].mComment = withinSingleLineComment;

						auto& endStr = mLanguageDefinition.mCommentEnd;
						if (currentCoord.mColumn + 1 >= (int)endStr.size() &&
							equals(endStr.begin(), endStr.end(), from + 1 - endStr.size(), from + 1, pred))
							commentStart = end;
					}
				}
				line[currentCoord.mColumn].mPreprocessor = withinPreproc;
			}
		}
		mCheckComments = false;
		return;
	}

	if (mColorRangeMin < mColorRangeMax)
	{
		const int increment = (mLanguageDefinition.mTokenize == nullptr) ? 10 : 10000;
		const int to = std::min(mColorRangeMin + increment, mColorRangeMax);
		ColorizeRange(mColorRangeMin, to);
		mColorRangeMin = to;

		if (mColorRangeMax == mColorRangeMin)
		{
			mColorRangeMin = std::numeric_limits<int>::max();
			mColorRangeMax = 0;
		}
		return;
	}
}

float TextEditor::TextDistanceToLineStart(const Coordinates& aFrom) const
{
	auto& line = mLines[aFrom.mLine];
	float distance = 0.0f;
	auto fontScale = ImGui::GetFontSize() / ImGui::GetFont()->FontSize;
	float spaceSize = ImGui::GetFont()->CalcTextSizeA(ImGui::GetFontSize(), FLT_MAX, -1.0f, (char*)" ", nullptr, nullptr).x;
	for (size_t it = 0u; it < line.size() && it < (unsigned)aFrom.mColumn; ++it)
	{
		if (line[it].mChar == L'\t')
		{
			distance = (1.0f * fontScale + std::floor((1.0f + distance)) / (float(mTabSize) * spaceSize)) * (float(mTabSize) * spaceSize);
		}
		else
		{
			Char tempCString[2];
			tempCString[0] = line[it].mChar;
			tempCString[1] = L'\0';
			std::string bstr;
			bstr = wstrtostr(tempCString);
			distance += ImGui::GetFont()->CalcTextSizeA(ImGui::GetFontSize(), FLT_MAX, -1.0f, (char*)bstr.c_str(), nullptr, nullptr).x;
		}
	}

	return distance;
}

void TextEditor::EnsureCursorVisible()
{
	if (!mWithinRender)
	{
		mScrollToCursor = true;
		return;
	}

	float scrollX = ImGui::GetScrollX();
	float scrollY = ImGui::GetScrollY();

	auto height = ImGui::GetWindowHeight();
	auto width = ImGui::GetWindowWidth();

	auto top = 1 + (int)ceil(scrollY / mCharAdvance.y);
	auto bottom = (int)ceil((scrollY + height) / mCharAdvance.y);

	auto left = (int)ceil(scrollX / mCharAdvance.x);
	auto right = (int)ceil((scrollX + width) / mCharAdvance.x);

	auto pos = GetActualCursorCoordinates();
	auto len = TextDistanceToLineStart(pos);

	if (pos.mLine < top)
		ImGui::SetScrollY(std::max(0.0f, (pos.mLine - 1) * mCharAdvance.y));
	if (pos.mLine > bottom - 4)
		ImGui::SetScrollY(std::max(0.0f, (pos.mLine + 4) * mCharAdvance.y - height));
	if (len + mTextStart < left + 4)
		ImGui::SetScrollX(std::max(0.0f, len + mTextStart - 4));
	if (len + mTextStart > right - 4)
		ImGui::SetScrollX(std::max(0.0f, len + mTextStart + 4 - width));
}

int TextEditor::GetPageSize() const
{
	auto height = ImGui::GetWindowHeight() - 20.0f;
	return (int)floor(height / mCharAdvance.y);
}

TextEditor::UndoRecord::UndoRecord(
	const std::wstring& aAdded,
	const TextEditor::Coordinates aAddedStart,
	const TextEditor::Coordinates aAddedEnd,
	const std::wstring& aRemoved,
	const TextEditor::Coordinates aRemovedStart,
	const TextEditor::Coordinates aRemovedEnd,
	TextEditor::EditorState& aBefore,
	TextEditor::EditorState& aAfter)
	: mAdded(aAdded)
	, mAddedStart(aAddedStart)
	, mAddedEnd(aAddedEnd)
	, mRemoved(aRemoved)
	, mRemovedStart(aRemovedStart)
	, mRemovedEnd(aRemovedEnd)
	, mBefore(aBefore)
	, mAfter(aAfter)
{
	assert(mAddedStart <= mAddedEnd);
	assert(mRemovedStart <= mRemovedEnd);
}

void TextEditor::UndoRecord::Undo(TextEditor * aEditor)
{
	if (!mAdded.empty())
	{
		aEditor->DeleteRange(mAddedStart, mAddedEnd);
		aEditor->Colorize(mAddedStart.mLine - 1, mAddedEnd.mLine - mAddedStart.mLine + 2);
	}

	if (!mRemoved.empty())
	{
		auto start = mRemovedStart;
		aEditor->InsertTextAt(start, mRemoved.c_str());
		aEditor->Colorize(mRemovedStart.mLine - 1, mRemovedEnd.mLine - mRemovedStart.mLine + 2);
	}

	aEditor->mState = mBefore;
	aEditor->EnsureCursorVisible();

}

void TextEditor::UndoRecord::Redo(TextEditor * aEditor)
{
	if (!mRemoved.empty())
	{
		aEditor->DeleteRange(mRemovedStart, mRemovedEnd);
		aEditor->Colorize(mRemovedStart.mLine - 1, mRemovedEnd.mLine - mRemovedStart.mLine + 1);
	}

	if (!mAdded.empty())
	{
		auto start = mAddedStart;
		aEditor->InsertTextAt(start, mAdded.c_str());
		aEditor->Colorize(mAddedStart.mLine - 1, mAddedEnd.mLine - mAddedStart.mLine + 1);
	}

	aEditor->mState = mAfter;
	aEditor->EnsureCursorVisible();
}

static bool TokenizeCStyleString(const TextEditor::Char * in_begin, const TextEditor::Char * in_end, const TextEditor::Char *& out_begin, const TextEditor::Char *& out_end)
{
	const TextEditor::Char * p = in_begin;

	if (*p == L'"')
	{
		p++;

		while (p < in_end)
		{
			// handle end of string
			if (*p == L'"')
			{
				out_begin = in_begin;
				out_end = p + 1;
				return true;
			}

			// handle escape character for L"
			if (*p == L'\\' && p + 1 < in_end && p[1] == L'"')
				p++;

			p++;
		}
	}

	return false;
}

static bool TokenizeCStyleCharacterLiteral(const TextEditor::Char * in_begin, const TextEditor::Char * in_end, const TextEditor::Char *& out_begin, const TextEditor::Char *& out_end)
{
	const TextEditor::Char * p = in_begin;

	if (*p == L'\'')
	{
		p++;

		// handle escape characters
		if (p < in_end && *p == L'\\')
			p++;

		if (p < in_end)
			p++;

		// handle end of character literal
		if (p < in_end && *p == L'\'')
		{
			out_begin = in_begin;
			out_end = p + 1;
			return true;
		}
	}

	return false;
}

static bool TokenizeCStyleIdentifier(const TextEditor::Char * in_begin, const TextEditor::Char * in_end, const TextEditor::Char *& out_begin, const TextEditor::Char *& out_end)
{
	const TextEditor::Char * p = in_begin;

	if ((*p >= L'a' && *p <= L'z') || (*p >= L'A' && *p <= L'Z') || *p == L'_')
	{
		p++;

		while ((p < in_end) && ((*p >= L'a' && *p <= L'z') || (*p >= L'A' && *p <= L'Z') || (*p >= L'0' && *p <= L'9') || *p == L'_'))
			p++;

		out_begin = in_begin;
		out_end = p;
		return true;
	}

	return false;
}

static bool TokenizeCStyleNumber(const TextEditor::Char * in_begin, const TextEditor::Char * in_end, const TextEditor::Char *& out_begin, const TextEditor::Char *& out_end)
{
	const TextEditor::Char * p = in_begin;

	const bool startsWithNumber = *p >= L'0' && *p <= L'9';

	if (*p != L'+' && *p != L'-' && !startsWithNumber)
		return false;

	p++;

	bool hasNumber = startsWithNumber;

	while (p < in_end && (*p >= L'0' && *p <= L'9'))
	{
		hasNumber = true;

		p++;
	}

	if (hasNumber == false)
		return false;

	bool isFloat = false;
	bool isHex = false;
	bool isBinary = false;

	if (p < in_end)
	{
		if (*p == L'.')
		{
			isFloat = true;

			p++;

			while (p < in_end && (*p >= L'0' && *p <= L'9'))
				p++;
		}
		else if (*p == L'x' || *p == L'X')
		{
			// hex formatted integer of the type 0xef80

			isHex = true;

			p++;

			while (p < in_end && ((*p >= L'0' && *p <= L'9') || (*p >= L'a' && *p <= L'f') || (*p >= L'A' && *p <= L'F')))
				p++;
		}
		else if (*p == L'b' || *p == L'B')
		{
			// binary formatted integer of the type 0b01011101

			isBinary = true;

			p++;

			while (p < in_end && (*p >= L'0' && *p <= L'1'))
				p++;
		}
	}

	if (isHex == false && isBinary == false)
	{
		// floating point exponent
		if (p < in_end && (*p == L'e' || *p == L'E'))
		{
			isFloat = true;

			p++;

			if (p < in_end && (*p == L'+' || *p == L'-'))
				p++;

			bool hasDigits = false;

			while (p < in_end && (*p >= L'0' && *p <= L'9'))
			{
				hasDigits = true;

				p++;
			}

			if (hasDigits == false)
				return false;
		}

		// single precision floating point type
		if (p < in_end && *p == L'f')
			p++;
	}

	if (isFloat == false)
	{
		// integer size type
		while (p < in_end && (*p == L'u' || *p == L'U' || *p == L'l' || *p == L'L'))
			p++;
	}

	out_begin = in_begin;
	out_end = p;
	return true;
}

static bool TokenizeCStylePunctuation(const TextEditor::Char * in_begin, const TextEditor::Char * in_end, const TextEditor::Char *& out_begin, const TextEditor::Char *& out_end)
{
	(void)in_end;

	switch (*in_begin)
	{
	case L'[':
	case L']':
	case L'{':
	case L'}':
	case L'!':
	case L'%':
	case L'^':
	case L'&':
	case L'*':
	case L'(':
	case L')':
	case L'-':
	case L'+':
	case L'=':
	case L'~':
	case L'|':
	case L'<':
	case L'>':
	case L'?':
	case L':':
	case L'/':
	case L';':
	case L',':
	case L'.':
		out_begin = in_begin;
		out_end = in_begin + 1;
		return true;
	}

	return false;
}

const TextEditor::LanguageDefinition& TextEditor::LanguageDefinition::CPlusPlus()
{
	static bool inited = false;
	static LanguageDefinition langDef;
	if (!inited)
	{
		static const Char* const cppKeywords[] = {
			L"alignas", L"alignof", L"and", L"and_eq", L"asm", L"atomic_cancel", L"atomic_commit", L"atomic_noexcept", L"auto", L"bitand", L"bitor", L"bool", L"break", L"case", L"catch", L"Char", L"char16_t", L"char32_t", L"class",
			L"compl", L"concept", L"const", L"constexpr", L"const_cast", L"continue", L"decltype", L"default", L"delete", L"do", L"double", L"dynamic_cast", L"else", L"enum", L"explicit", L"export", L"extern", L"false", L"float",
			L"for", L"friend", L"goto", L"if", L"import", L"inline", L"int", L"long", L"module", L"mutable", L"namespace", L"new", L"noexcept", L"not", L"not_eq", L"nullptr", L"operator", L"or", L"or_eq", L"private", L"protected", L"public",
			L"register", L"reinterpret_cast", L"requires", L"return", L"short", L"signed", L"sizeof", L"static", L"static_assert", L"static_cast", L"struct", L"switch", L"synchronized", L"template", L"this", L"thread_local",
			L"throw", L"true", L"try", L"typedef", L"typeid", L"typename", L"union", L"unsigned", L"using", L"virtual", L"void", L"volatile", L"Char", L"while", L"xor", L"xor_eq"
		};
		for (auto& k : cppKeywords)
			langDef.mKeywords.insert(k);
/*
		static const Char* const identifiers[] = {
			"abort", L"abs", L"acos", L"asin", L"atan", L"atexit", L"atof", L"atoi", L"atol", L"ceil", L"clock", L"cosh", L"ctime", L"div", L"exit", L"fabs", L"floor", L"fmod", L"getchar", L"getenv", L"isalnum", L"isalpha", L"isdigit", L"isgraph",
			"ispunct", L"isspace", L"isupper", L"kbhit", L"log10", L"log2", L"log", L"memcmp", L"modf", L"pow", L"printf", L"sprintf", L"_snwprintf", L"putchar", L"putenv", L"puts", L"rand", L"remove", L"rename", L"sinh", L"sqrt", L"srand", L"strcat", L"strcmp", L"strerror", L"time", L"tolower", L"toupper",
			"std", L"string", L"vector", L"map", L"unordered_map", L"set", L"unordered_set", L"min", L"max"
		};
		for (auto& k : identifiers)
		{
			Identifier id;
			id.mDeclaration = L"Built-in function";
			langDef.mIdentifiers.insert(std::make_pair(std::wstring(k), id));
		}
*/
		langDef.mTokenize = [](const Char * in_begin, const Char * in_end, const Char *& out_begin, const Char *& out_end, PaletteIndex & paletteIndex) -> bool
		{
			paletteIndex = PaletteIndex::Max;

			while (in_begin < in_end && isblank(*in_begin))
				in_begin++;

			if (in_begin == in_end)
			{
				out_begin = in_end;
				out_end = in_end;
				paletteIndex = PaletteIndex::Default;
			}
			else if (TokenizeCStyleString(in_begin, in_end, out_begin, out_end))
				paletteIndex = PaletteIndex::String;
			else if (TokenizeCStyleCharacterLiteral(in_begin, in_end, out_begin, out_end))
				paletteIndex = PaletteIndex::CharLiteral;
			else if (TokenizeCStyleIdentifier(in_begin, in_end, out_begin, out_end))
				paletteIndex = PaletteIndex::Identifier;
			else if (TokenizeCStyleNumber(in_begin, in_end, out_begin, out_end))
				paletteIndex = PaletteIndex::Number;
			else if (TokenizeCStylePunctuation(in_begin, in_end, out_begin, out_end))
				paletteIndex = PaletteIndex::Punctuation;

			return paletteIndex != PaletteIndex::Max;
		};

		langDef.mCommentStart = L"/*";
		langDef.mCommentEnd = L"*/";
		langDef.mSingleLineComment = L"//";

		langDef.mCaseSensitive = true;
		langDef.mAutoIndentation = true;

		langDef.mName = L"C++";

		inited = true;
	}
	return langDef;
}

const TextEditor::LanguageDefinition& TextEditor::LanguageDefinition::HLSL()
{
	static bool inited = false;
	static LanguageDefinition langDef;
	if (!inited)
	{
		static const Char* const keywords[] = {
			L"AppendStructuredBuffer", L"asm", L"asm_fragment", L"BlendState", L"bool", L"break", L"Buffer", L"ByteAddressBuffer", L"case", L"cbuffer", L"centroid", L"class", L"column_major", L"compile", L"compile_fragment",
			L"CompileShader", L"const", L"continue", L"ComputeShader", L"ConsumeStructuredBuffer", L"default", L"DepthStencilState", L"DepthStencilView", L"discard", L"do", L"double", L"DomainShader", L"dword", L"else",
			L"export", L"extern", L"false", L"float", L"for", L"fxgroup", L"GeometryShader", L"groupshared", L"half", L"Hullshader", L"if", L"in", L"inline", L"inout", L"InputPatch", L"int", L"interface", L"line", L"lineadj",
			L"linear", L"LineStream", L"matrix", L"min16float", L"min10float", L"min16int", L"min12int", L"min16uint", L"namespace", L"nointerpolation", L"noperspective", L"NULL", L"out", L"OutputPatch", L"packoffset",
			L"pass", L"pixelfragment", L"PixelShader", L"point", L"PointStream", L"precise", L"RasterizerState", L"RenderTargetView", L"return", L"register", L"row_major", L"RWBuffer", L"RWByteAddressBuffer", L"RWStructuredBuffer",
			L"RWTexture1D", L"RWTexture1DArray", L"RWTexture2D", L"RWTexture2DArray", L"RWTexture3D", L"sample", L"sampler", L"SamplerState", L"SamplerComparisonState", L"shared", L"snorm", L"stateblock", L"stateblock_state",
			L"static", L"string", L"struct", L"switch", L"StructuredBuffer", L"tbuffer", L"technique", L"technique10", L"technique11", L"texture", L"Texture1D", L"Texture1DArray", L"Texture2D", L"Texture2DArray", L"Texture2DMS",
			L"Texture2DMSArray", L"Texture3D", L"TextureCube", L"TextureCubeArray", L"true", L"typedef", L"triangle", L"triangleadj", L"TriangleStream", L"uint", L"uniform", L"unorm", L"unsigned", L"vector", L"vertexfragment",
			L"VertexShader", L"void", L"volatile", L"while",
			L"bool1",L"bool2",L"bool3",L"bool4",L"double1",L"double2",L"double3",L"double4", L"float1", L"float2", L"float3", L"float4", L"int1", L"int2", L"int3", L"int4", L"in", L"out", L"inout",
			L"uint1", L"uint2", L"uint3", L"uint4", L"dword1", L"dword2", L"dword3", L"dword4", L"half1", L"half2", L"half3", L"half4",
			L"float1x1",L"float2x1",L"float3x1",L"float4x1",L"float1x2",L"float2x2",L"float3x2",L"float4x2",
			L"float1x3",L"float2x3",L"float3x3",L"float4x3",L"float1x4",L"float2x4",L"float3x4",L"float4x4",
			L"half1x1",L"half2x1",L"half3x1",L"half4x1",L"half1x2",L"half2x2",L"half3x2",L"half4x2",
			L"half1x3",L"half2x3",L"half3x3",L"half4x3",L"half1x4",L"half2x4",L"half3x4",L"half4x4",
		};
		for (auto& k : keywords)
			langDef.mKeywords.insert(k);

		static const Char* const identifiers[] = {
			L"abort", L"abs", L"acos", L"all", L"AllMemoryBarrier", L"AllMemoryBarrierWithGroupSync", L"any", L"asdouble", L"asfloat", L"asin", L"asint", L"asint", L"asuint",
			L"asuint", L"atan", L"atan2", L"ceil", L"CheckAccessFullyMapped", L"clamp", L"clip", L"cos", L"cosh", L"countbits", L"cross", L"D3DCOLORtoUBYTE4", L"ddx",
			L"ddx_coarse", L"ddx_fine", L"ddy", L"ddy_coarse", L"ddy_fine", L"degrees", L"determinant", L"DeviceMemoryBarrier", L"DeviceMemoryBarrierWithGroupSync",
			L"distance", L"dot", L"dst", L"errorf", L"EvaluateAttributeAtCentroid", L"EvaluateAttributeAtSample", L"EvaluateAttributeSnapped", L"exp", L"exp2",
			L"f16tof32", L"f32tof16", L"faceforward", L"firstbithigh", L"firstbitlow", L"floor", L"fma", L"fmod", L"frac", L"frexp", L"fwidth", L"GetRenderTargetSampleCount",
			L"GetRenderTargetSamplePosition", L"GroupMemoryBarrier", L"GroupMemoryBarrierWithGroupSync", L"InterlockedAdd", L"InterlockedAnd", L"InterlockedCompareExchange",
			L"InterlockedCompareStore", L"InterlockedExchange", L"InterlockedMax", L"InterlockedMin", L"InterlockedOr", L"InterlockedXor", L"isfinite", L"isinf", L"isnan",
			L"ldexp", L"length", L"lerp", L"lit", L"log", L"log10", L"log2", L"mad", L"max", L"min", L"modf", L"msad4", L"mul", L"noise", L"normalize", L"pow", L"printf",
			L"Process2DQuadTessFactorsAvg", L"Process2DQuadTessFactorsMax", L"Process2DQuadTessFactorsMin", L"ProcessIsolineTessFactors", L"ProcessQuadTessFactorsAvg",
			L"ProcessQuadTessFactorsMax", L"ProcessQuadTessFactorsMin", L"ProcessTriTessFactorsAvg", L"ProcessTriTessFactorsMax", L"ProcessTriTessFactorsMin",
			L"radians", L"rcp", L"reflect", L"refract", L"reversebits", L"round", L"rsqrt", L"saturate", L"sign", L"sin", L"sincos", L"sinh", L"smoothstep", L"sqrt", L"step",
			L"tan", L"tanh", L"tex1D", L"tex1D", L"tex1Dbias", L"tex1Dgrad", L"tex1Dlod", L"tex1Dproj", L"tex2D", L"tex2D", L"tex2Dbias", L"tex2Dgrad", L"tex2Dlod", L"tex2Dproj",
			L"tex3D", L"tex3D", L"tex3Dbias", L"tex3Dgrad", L"tex3Dlod", L"tex3Dproj", L"texCUBE", L"texCUBE", L"texCUBEbias", L"texCUBEgrad", L"texCUBElod", L"texCUBEproj", L"transpose", L"trunc"
		};

		for (auto& k : identifiers)
		{
			Identifier id;
			id.mDeclaration = L"Built-in function";
			langDef.mIdentifiers.insert(std::make_pair(std::wstring(k), id));
		}

		langDef.mTokenRegexStrings.push_back(std::make_pair<std::wstring, PaletteIndex>(L"[ \\t]*#[ \\t]*[a-zA-ZР-пр-џ_]+", PaletteIndex::Preprocessor));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::wstring, PaletteIndex>(L"L?\\\"(\\\\.|[^\\\"])*\\\"", PaletteIndex::String));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::wstring, PaletteIndex>(L"\\'\\\\?[^\\']\\'", PaletteIndex::CharLiteral));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::wstring, PaletteIndex>(L"[+-]?([0-9]+([.][0-9]*)?|[.][0-9]+)([eE][+-]?[0-9]+)?[fF]?", PaletteIndex::Number));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::wstring, PaletteIndex>(L"[+-]?[0-9]+[Uu]?[lL]?[lL]?", PaletteIndex::Number));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::wstring, PaletteIndex>(L"0[0-7]+[Uu]?[lL]?[lL]?", PaletteIndex::Number));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::wstring, PaletteIndex>(L"0[xX][0-9a-fA-F]+[uU]?[lL]?[lL]?", PaletteIndex::Number));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::wstring, PaletteIndex>(L"[a-zA-ZР-пр-џ_][a-zA-ZР-пр-џ0-9_]*", PaletteIndex::Identifier));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::wstring, PaletteIndex>(L"[\\[\\]\\{\\}\\!\\%\\^\\&\\*\\(\\)\\-\\+\\=\\~\\|\\<\\>\\?\\/\\;\\,\\.]", PaletteIndex::Punctuation));

		langDef.mCommentStart = L"/*";
		langDef.mCommentEnd = L"*/";
		langDef.mSingleLineComment = L"//";

		langDef.mCaseSensitive = true;
		langDef.mAutoIndentation = true;

		langDef.mName = L"HLSL";

		inited = true;
	}
	return langDef;
}

const TextEditor::LanguageDefinition& TextEditor::LanguageDefinition::GLSL()
{
	static bool inited = false;
	static LanguageDefinition langDef;
	if (!inited)
	{
		static const Char* const keywords[] = {
			L"auto", L"break", L"case", L"Char", L"const", L"continue", L"default", L"do", L"double", L"else", L"enum", L"extern", L"float", L"for", L"goto", L"if", L"inline", L"int", L"long", L"register", L"restrict", L"return", L"short",
			L"signed", L"sizeof", L"static", L"struct", L"switch", L"typedef", L"union", L"unsigned", L"void", L"volatile", L"while", L"_Alignas", L"_Alignof", L"_Atomic", L"_Bool", L"_Complex", L"_Generic", L"_Imaginary",
			L"_Noreturn", L"_Static_assert", L"_Thread_local"
		};
		for (auto& k : keywords)
			langDef.mKeywords.insert(k);

		static const Char* const identifiers[] = {
			L"abort", L"abs", L"acos", L"asin", L"atan", L"atexit", L"atof", L"atoi", L"atol", L"ceil", L"clock", L"cosh", L"ctime", L"div", L"exit", L"fabs", L"floor", L"fmod", L"getchar", L"getenv", L"isalnum", L"isalpha", L"isdigit", L"isgraph",
			L"ispunct", L"isspace", L"isupper", L"kbhit", L"log10", L"log2", L"log", L"memcmp", L"modf", L"pow", L"putchar", L"putenv", L"puts", L"rand", L"remove", L"rename", L"sinh", L"sqrt", L"srand", L"strcat", L"strcmp", L"strerror", L"time", L"tolower", L"toupper"
		};
		for (auto& k : identifiers)
		{
			Identifier id;
			id.mDeclaration = L"Built-in function";
			langDef.mIdentifiers.insert(std::make_pair(std::wstring(k), id));
		}

		langDef.mTokenRegexStrings.push_back(std::make_pair<std::wstring, PaletteIndex>(L"[ \\t]*#[ \\t]*[a-zA-ZР-пр-џ_]+", PaletteIndex::Preprocessor));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::wstring, PaletteIndex>(L"L?\\\"(\\\\.|[^\\\"])*\\\"", PaletteIndex::String));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::wstring, PaletteIndex>(L"\\'\\\\?[^\\']\\'", PaletteIndex::CharLiteral));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::wstring, PaletteIndex>(L"[+-]?([0-9]+([.][0-9]*)?|[.][0-9]+)([eE][+-]?[0-9]+)?[fF]?", PaletteIndex::Number));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::wstring, PaletteIndex>(L"[+-]?[0-9]+[Uu]?[lL]?[lL]?", PaletteIndex::Number));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::wstring, PaletteIndex>(L"0[0-7]+[Uu]?[lL]?[lL]?", PaletteIndex::Number));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::wstring, PaletteIndex>(L"0[xX][0-9a-fA-F]+[uU]?[lL]?[lL]?", PaletteIndex::Number));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::wstring, PaletteIndex>(L"[a-zA-ZР-пр-џ_][a-zA-ZР-пр-џ0-9_]*", PaletteIndex::Identifier));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::wstring, PaletteIndex>(L"[\\[\\]\\{\\}\\!\\%\\^\\&\\*\\(\\)\\-\\+\\=\\~\\|\\<\\>\\?\\/\\;\\,\\.]", PaletteIndex::Punctuation));

		langDef.mCommentStart = L"/*";
		langDef.mCommentEnd = L"*/";
		langDef.mSingleLineComment = L"//";

		langDef.mCaseSensitive = true;
		langDef.mAutoIndentation = true;

		langDef.mName = L"GLSL";

		inited = true;
	}
	return langDef;
}

const TextEditor::LanguageDefinition& TextEditor::LanguageDefinition::C()
{
	static bool inited = false;
	static LanguageDefinition langDef;
	if (!inited)
	{
		static const Char* const keywords[] = {
			L"auto", L"break", L"case", L"Char", L"const", L"continue", L"default", L"do", L"double", L"else", L"enum", L"extern", L"float", L"for", L"goto", L"if", L"inline", L"int", L"long", L"register", L"restrict", L"return", L"short",
			L"signed", L"sizeof", L"static", L"struct", L"switch", L"typedef", L"union", L"unsigned", L"void", L"volatile", L"while", L"_Alignas", L"_Alignof", L"_Atomic", L"_Bool", L"_Complex", L"_Generic", L"_Imaginary",
			L"_Noreturn", L"_Static_assert", L"_Thread_local"
		};
		for (auto& k : keywords)
			langDef.mKeywords.insert(k);

		static const Char* const identifiers[] = {
			L"abort", L"abs", L"acos", L"asin", L"atan", L"atexit", L"atof", L"atoi", L"atol", L"ceil", L"clock", L"cosh", L"ctime", L"div", L"exit", L"fabs", L"floor", L"fmod", L"getchar", L"getenv", L"isalnum", L"isalpha", L"isdigit", L"isgraph",
			L"ispunct", L"isspace", L"isupper", L"kbhit", L"log10", L"log2", L"log", L"memcmp", L"modf", L"pow", L"putchar", L"putenv", L"puts", L"rand", L"remove", L"rename", L"sinh", L"sqrt", L"srand", L"strcat", L"strcmp", L"strerror", L"time", L"tolower", L"toupper"
		};
		for (auto& k : identifiers)
		{
			Identifier id;
			id.mDeclaration = L"Built-in function";
			langDef.mIdentifiers.insert(std::make_pair(std::wstring(k), id));
		}

		langDef.mTokenize = [](const Char * in_begin, const Char * in_end, const Char *& out_begin, const Char *& out_end, PaletteIndex & paletteIndex) -> bool
		{
			paletteIndex = PaletteIndex::Max;

			while (in_begin < in_end && isblank(*in_begin))
				in_begin++;

			if (in_begin == in_end)
			{
				out_begin = in_end;
				out_end = in_end;
				paletteIndex = PaletteIndex::Default;
			}
			else if (TokenizeCStyleString(in_begin, in_end, out_begin, out_end))
				paletteIndex = PaletteIndex::String;
			else if (TokenizeCStyleCharacterLiteral(in_begin, in_end, out_begin, out_end))
				paletteIndex = PaletteIndex::CharLiteral;
			else if (TokenizeCStyleIdentifier(in_begin, in_end, out_begin, out_end))
				paletteIndex = PaletteIndex::Identifier;
			else if (TokenizeCStyleNumber(in_begin, in_end, out_begin, out_end))
				paletteIndex = PaletteIndex::Number;
			else if (TokenizeCStylePunctuation(in_begin, in_end, out_begin, out_end))
				paletteIndex = PaletteIndex::Punctuation;

			return paletteIndex != PaletteIndex::Max;
		};

		langDef.mCommentStart = L"/*";
		langDef.mCommentEnd = L"*/";
		langDef.mSingleLineComment = L"//";

		langDef.mCaseSensitive = true;
		langDef.mAutoIndentation = true;

		langDef.mName = L"C";

		inited = true;
	}
	return langDef;
}

const TextEditor::LanguageDefinition& TextEditor::LanguageDefinition::SQL()
{
	static bool inited = false;
	static LanguageDefinition langDef;
	if (!inited)
	{
		static const Char* const keywords[] = {
			L"ADD", L"EXCEPT", L"PERCENT", L"ALL", L"EXEC", L"PLAN", L"ALTER", L"EXECUTE", L"PRECISION", L"AND", L"EXISTS", L"PRIMARY", L"ANY", L"EXIT", L"PRINT", L"AS", L"FETCH", L"PROC", L"ASC", L"FILE", L"PROCEDURE",
			L"AUTHORIZATION", L"FILLFACTOR", L"PUBLIC", L"BACKUP", L"FOR", L"RAISERROR", L"BEGIN", L"FOREIGN", L"READ", L"BETWEEN", L"FREETEXT", L"READTEXT", L"BREAK", L"FREETEXTTABLE", L"RECONFIGURE",
			L"BROWSE", L"FROM", L"REFERENCES", L"BULK", L"FULL", L"REPLICATION", L"BY", L"FUNCTION", L"RESTORE", L"CASCADE", L"GOTO", L"RESTRICT", L"CASE", L"GRANT", L"RETURN", L"CHECK", L"GROUP", L"REVOKE",
			L"CHECKPOINT", L"HAVING", L"RIGHT", L"CLOSE", L"HOLDLOCK", L"ROLLBACK", L"CLUSTERED", L"IDENTITY", L"ROWCOUNT", L"COALESCE", L"IDENTITY_INSERT", L"ROWGUIDCOL", L"COLLATE", L"IDENTITYCOL", L"RULE",
			L"COLUMN", L"IF", L"SAVE", L"COMMIT", L"IN", L"SCHEMA", L"COMPUTE", L"INDEX", L"SELECT", L"CONSTRAINT", L"INNER", L"SESSION_USER", L"CONTAINS", L"INSERT", L"SET", L"CONTAINSTABLE", L"INTERSECT", L"SETUSER",
			L"CONTINUE", L"INTO", L"SHUTDOWN", L"CONVERT", L"IS", L"SOME", L"CREATE", L"JOIN", L"STATISTICS", L"CROSS", L"KEY", L"SYSTEM_USER", L"CURRENT", L"KILL", L"TABLE", L"CURRENT_DATE", L"LEFT", L"TEXTSIZE",
			L"CURRENT_TIME", L"LIKE", L"THEN", L"CURRENT_TIMESTAMP", L"LINENO", L"TO", L"CURRENT_USER", L"LOAD", L"TOP", L"CURSOR", L"NATIONAL", L"TRAN", L"DATABASE", L"NOCHECK", L"TRANSACTION",
			L"DBCC", L"NONCLUSTERED", L"TRIGGER", L"DEALLOCATE", L"NOT", L"TRUNCATE", L"DECLARE", L"NULL", L"TSEQUAL", L"DEFAULT", L"NULLIF", L"UNION", L"DELETE", L"OF", L"UNIQUE", L"DENY", L"OFF", L"UPDATE",
			L"DESC", L"OFFSETS", L"UPDATETEXT", L"DISK", L"ON", L"USE", L"DISTINCT", L"OPEN", L"USER", L"DISTRIBUTED", L"OPENDATASOURCE", L"VALUES", L"DOUBLE", L"OPENQUERY", L"VARYING",L"DROP", L"OPENROWSET", L"VIEW",
			L"DUMMY", L"OPENXML", L"WAITFOR", L"DUMP", L"OPTION", L"WHEN", L"ELSE", L"OR", L"WHERE", L"END", L"ORDER", L"WHILE", L"ERRLVL", L"OUTER", L"WITH", L"ESCAPE", L"OVER", L"WRITETEXT"
		};

		for (auto& k : keywords)
			langDef.mKeywords.insert(k);

		static const Char* const identifiers[] = {
			L"ABS",  L"ACOS",  L"ADD_MONTHS",  L"ASCII",  L"ASCIISTR",  L"ASIN",  L"ATAN",  L"ATAN2",  L"AVG",  L"BFILENAME",  L"BIN_TO_NUM",  L"BITAND",  L"CARDINALITY",  L"CASE",  L"CAST",  L"CEIL",
			L"CHARTOROWID",  L"CHR",  L"COALESCE",  L"COMPOSE",  L"CONCAT",  L"CONVERT",  L"CORR",  L"COS",  L"COSH",  L"COUNT",  L"COVAR_POP",  L"COVAR_SAMP",  L"CUME_DIST",  L"CURRENT_DATE",
			L"CURRENT_TIMESTAMP",  L"DBTIMEZONE",  L"DECODE",  L"DECOMPOSE",  L"DENSE_RANK",  L"DUMP",  L"EMPTY_BLOB",  L"EMPTY_CLOB",  L"EXP",  L"EXTRACT",  L"FIRST_VALUE",  L"FLOOR",  L"FROM_TZ",  L"GREATEST",
			L"GROUP_ID",  L"HEXTORAW",  L"INITCAP",  L"INSTR",  L"INSTR2",  L"INSTR4",  L"INSTRB",  L"INSTRC",  L"LAG",  L"LAST_DAY",  L"LAST_VALUE",  L"LEAD",  L"LEAST",  L"LENGTH",  L"LENGTH2",  L"LENGTH4",
			L"LENGTHB",  L"LENGTHC",  L"LISTAGG",  L"LN",  L"LNNVL",  L"LOCALTIMESTAMP",  L"LOG",  L"LOWER",  L"LPAD",  L"LTRIM",  L"MAX",  L"MEDIAN",  L"MIN",  L"MOD",  L"MONTHS_BETWEEN",  L"NANVL",  L"NCHR",
			L"NEW_TIME",  L"NEXT_DAY",  L"NTH_VALUE",  L"NULLIF",  L"NUMTODSINTERVAL",  L"NUMTOYMINTERVAL",  L"NVL",  L"NVL2",  L"POWER",  L"RANK",  L"RAWTOHEX",  L"REGEXP_COUNT",  L"REGEXP_INSTR",
			L"REGEXP_REPLACE",  L"REGEXP_SUBSTR",  L"REMAINDER",  L"REPLACE",  L"ROUND",  L"ROWNUM",  L"RPAD",  L"RTRIM",  L"SESSIONTIMEZONE",  L"SIGN",  L"SIN",  L"SINH",
			L"SOUNDEX",  L"SQRT",  L"STDDEV",  L"SUBSTR",  L"SUM",  L"SYS_CONTEXT",  L"SYSDATE",  L"SYSTIMESTAMP",  L"TAN",  L"TANH",  L"TO_CHAR",  L"TO_CLOB",  L"TO_DATE",  L"TO_DSINTERVAL",  L"TO_LOB",
			L"TO_MULTI_BYTE",  L"TO_NCLOB",  L"TO_NUMBER",  L"TO_SINGLE_BYTE",  L"TO_TIMESTAMP",  L"TO_TIMESTAMP_TZ",  L"TO_YMINTERVAL",  L"TRANSLATE",  L"TRIM",  L"TRUNC", L"TZ_OFFSET",  L"UID",  L"UPPER",
			L"USER",  L"USERENV",  L"VAR_POP",  L"VAR_SAMP",  L"VARIANCE",  L"VSIZE L"
		};
		for (auto& k : identifiers)
		{
			Identifier id;
			id.mDeclaration = L"Built-in function";
			langDef.mIdentifiers.insert(std::make_pair(std::wstring(k), id));
		}

		langDef.mTokenRegexStrings.push_back(std::make_pair<std::wstring, PaletteIndex>(L"L?\\\"(\\\\.|[^\\\"])*\\\"", PaletteIndex::String));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::wstring, PaletteIndex>(L"\\\'[^\\\']*\\\'", PaletteIndex::String));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::wstring, PaletteIndex>(L"[+-]?([0-9]+([.][0-9]*)?|[.][0-9]+)([eE][+-]?[0-9]+)?[fF]?", PaletteIndex::Number));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::wstring, PaletteIndex>(L"[+-]?[0-9]+[Uu]?[lL]?[lL]?", PaletteIndex::Number));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::wstring, PaletteIndex>(L"0[0-7]+[Uu]?[lL]?[lL]?", PaletteIndex::Number));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::wstring, PaletteIndex>(L"0[xX][0-9a-fA-F]+[uU]?[lL]?[lL]?", PaletteIndex::Number));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::wstring, PaletteIndex>(L"[a-zA-ZР-пр-џ_][a-zA-ZР-пр-џ0-9_]*", PaletteIndex::Identifier));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::wstring, PaletteIndex>(L"[\\[\\]\\{\\}\\!\\%\\^\\&\\*\\(\\)\\-\\+\\=\\~\\|\\<\\>\\?\\/\\;\\,\\.]", PaletteIndex::Punctuation));

		langDef.mCommentStart = L"/*";
		langDef.mCommentEnd = L"*/";
		langDef.mSingleLineComment = L"//";

		langDef.mCaseSensitive = false;
		langDef.mAutoIndentation = false;

		langDef.mName = L"SQL";

		inited = true;
	}
	return langDef;
}

const TextEditor::LanguageDefinition& TextEditor::LanguageDefinition::AngelScript()
{
	static bool inited = false;
	static LanguageDefinition langDef;
	if (!inited)
	{
		static const Char* const keywords[] = {
			L"and", L"abstract", L"auto", L"bool", L"break", L"case", L"cast", L"class", L"const", L"continue", L"default", L"do", L"double", L"else", L"enum", L"false", L"final", L"float", L"for",
			L"from", L"funcdef", L"function", L"get", L"if", L"import", L"in", L"inout", L"int", L"interface", L"int8", L"int16", L"int32", L"int64", L"is", L"mixin", L"namespace", L"not",
			L"null", L"or", L"out", L"override", L"private", L"protected", L"return", L"set", L"shared", L"super", L"switch", L"this L", L"true", L"typedef", L"uint", L"uint8", L"uint16", L"uint32",
			L"uint64", L"void", L"while", L"xor"
		};

		for (auto& k : keywords)
			langDef.mKeywords.insert(k);

		static const Char* const identifiers[] = {
			L"cos", L"sin", L"tab", L"acos", L"asin", L"atan", L"atan2", L"cosh", L"sinh", L"tanh", L"log", L"log10", L"pow", L"sqrt", L"abs", L"ceil", L"floor", L"fraction", L"closeTo", L"fpFromIEEE", L"fpToIEEE",
			L"complex", L"opEquals", L"opAddAssign", L"opSubAssign", L"opMulAssign", L"opDivAssign", L"opAdd", L"opSub", L"opMul", L"opDiv"
		};
		for (auto& k : identifiers)
		{
			Identifier id;
			id.mDeclaration = L"Built-in function";
			langDef.mIdentifiers.insert(std::make_pair(std::wstring(k), id));
		}

		langDef.mTokenRegexStrings.push_back(std::make_pair<std::wstring, PaletteIndex>(L"L?\\\"(\\\\.|[^\\\"])*\\\"", PaletteIndex::String));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::wstring, PaletteIndex>(L"\\'\\\\?[^\\']\\'", PaletteIndex::String));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::wstring, PaletteIndex>(L"[+-]?([0-9]+([.][0-9]*)?|[.][0-9]+)([eE][+-]?[0-9]+)?[fF]?", PaletteIndex::Number));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::wstring, PaletteIndex>(L"[+-]?[0-9]+[Uu]?[lL]?[lL]?", PaletteIndex::Number));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::wstring, PaletteIndex>(L"0[0-7]+[Uu]?[lL]?[lL]?", PaletteIndex::Number));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::wstring, PaletteIndex>(L"0[xX][0-9a-fA-F]+[uU]?[lL]?[lL]?", PaletteIndex::Number));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::wstring, PaletteIndex>(L"[a-zA-ZР-пр-џ_][a-zA-ZР-пр-џ0-9_]*", PaletteIndex::Identifier));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::wstring, PaletteIndex>(L"[\\[\\]\\{\\}\\!\\%\\^\\&\\*\\(\\)\\-\\+\\=\\~\\|\\<\\>\\?\\/\\;\\,\\.]", PaletteIndex::Punctuation));

		langDef.mCommentStart = L"/*";
		langDef.mCommentEnd = L"*/";
		langDef.mSingleLineComment = L"//";

		langDef.mCaseSensitive = true;
		langDef.mAutoIndentation = true;

		langDef.mName = L"AngelScript";

		inited = true;
	}
	return langDef;
}

const TextEditor::LanguageDefinition& TextEditor::LanguageDefinition::Lua()
{
	static bool inited = false;
	static LanguageDefinition langDef;
	if (!inited)
	{
		static const Char* const keywords[] = {
			L"and", L"break", L"do", L"", L"else", L"elseif", L"end", L"false", L"for", L"function", L"if", L"in", L"", L"local", L"nil", L"not", L"or", L"repeat", L"return", L"then", L"true", L"until", L"while"
		};

		for (auto& k : keywords)
			langDef.mKeywords.insert(k);

		static const Char* const identifiers[] = {
			L"assert", L"collectgarbage", L"dofile", L"error", L"getmetatable", L"ipairs", L"loadfile", L"load", L"loadstring",  L"next",  L"pairs",  L"pcall",  L"print",  L"rawequal",  L"rawlen",  L"rawget",  L"rawset",
			L"select",  L"setmetatable",  L"tonumber",  L"tostring",  L"type",  L"xpcall",  L"_G",  L"_VERSION",L"arshift", L"band", L"bnot", L"bor", L"bxor", L"btest", L"extract", L"lrotate", L"lshift", L"replace",
			L"rrotate", L"rshift", L"create", L"resume", L"running", L"status", L"wrap", L"yield", L"isyieldable", L"debug",L"getuservalue", L"gethook", L"getinfo", L"getlocal", L"getregistry", L"getmetatable",
			L"getupvalue", L"upvaluejoin", L"upvalueid", L"setuservalue", L"sethook", L"setlocal", L"setmetatable", L"setupvalue", L"traceback", L"close", L"flush", L"input", L"lines", L"open", L"output", L"popen",
			L"read", L"tmpfile", L"type", L"write", L"close", L"flush", L"lines", L"read", L"seek", L"setvbuf", L"write", L"__gc", L"__tostring", L"abs", L"acos", L"asin", L"atan", L"ceil", L"cos", L"deg", L"exp", L"tointeger",
			L"floor", L"fmod", L"ult", L"log", L"max", L"min", L"modf", L"rad", L"random", L"randomseed", L"sin", L"sqrt", L"string", L"tan", L"type", L"atan2", L"cosh", L"sinh", L"tanh",
			 L"pow", L"frexp", L"ldexp", L"log10", L"pi", L"huge", L"maxinteger", L"mininteger", L"loadlib", L"searchpath", L"seeall", L"preload", L"cpath", L"path", L"searchers", L"loaded", L"module", L"require", L"clock",
			 L"date", L"difftime", L"execute", L"exit", L"getenv", L"remove", L"rename", L"setlocale", L"time", L"tmpname", L"byte", L"Char", L"dump", L"find", L"format", L"gmatch", L"gsub", L"len", L"lower", L"match", L"rep",
			 L"reverse", L"sub", L"upper", L"pack", L"packsize", L"unpack", L"concat", L"maxn", L"insert", L"pack", L"unpack", L"remove", L"move", L"sort", L"offset", L"codepoint", L"Char", L"len", L"codes", L"charpattern",
			 L"coroutine", L"table", L"io", L"os", L"string", L"utf8", L"bit32", L"math", L"debug", L"package"
		};
		for (auto& k : identifiers)
		{
			Identifier id;
			id.mDeclaration = L"Built-in function";
			langDef.mIdentifiers.insert(std::make_pair(std::wstring(k), id));
		}

		langDef.mTokenRegexStrings.push_back(std::make_pair<std::wstring, PaletteIndex>(L"L?\\\"(\\\\.|[^\\\"])*\\\"", PaletteIndex::String));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::wstring, PaletteIndex>(L"\\\'[^\\\']*\\\'", PaletteIndex::String));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::wstring, PaletteIndex>(L"0[xX][0-9a-fA-F]+[uU]?[lL]?[lL]?", PaletteIndex::Number));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::wstring, PaletteIndex>(L"[+-]?([0-9]+([.][0-9]*)?|[.][0-9]+)([eE][+-]?[0-9]+)?[fF]?", PaletteIndex::Number));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::wstring, PaletteIndex>(L"[+-]?[0-9]+[Uu]?[lL]?[lL]?", PaletteIndex::Number));
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::wstring, PaletteIndex>(L"[a-zA-ZР-пр-џ_][a-zA-ZР-пр-џ0-9_]*", PaletteIndex::Identifier));		
		langDef.mTokenRegexStrings.push_back(std::make_pair<std::wstring, PaletteIndex>(L"[\\[\\]\\{\\}\\!\\%\\^\\&\\*\\(\\)\\-\\+\\=\\~\\|\\<\\>\\?\\/\\;\\,\\.]", PaletteIndex::Punctuation));

		langDef.mCommentStart = L"--[[";
		langDef.mCommentEnd = L"]]";
		langDef.mSingleLineComment = L"--";

		langDef.mCaseSensitive = true;
		langDef.mAutoIndentation = false;

		langDef.mName = L"Lua";

		inited = true;
	}
	return langDef;
}
