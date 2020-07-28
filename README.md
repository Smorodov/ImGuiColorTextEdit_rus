# ImGuiColorTextEdit_rus

Mainly based on https://github.com/BalazsJako/ImGuiColorTextEdit project.
Modified to woek with wide chars, tested in visual studio 2019. 

Syntax highlighting text editor for ImGui

# Main features
 - approximates typical code editor look and feel (essential mouse/keyboard commands work - I mean, the commands _I_ normally use :))
 - undo/redo support
 - extensible, multiple language syntax support
 - identifier declarations: a small piece of text associated with an identifier. The editor displays it in a tooltip when the mouse cursor is hovered over the identifier
 - error markers: the user can specify a list of error messages together the line of occurence, the editor will highligh the lines with red backround and display error message in a tooltip when the mouse cursor is hovered over the line
 - supports large files: there is no explicit limit set on file size or number of lines, performance is not affected when large files are loaded (except syntax coloring, see below)
 - color palette support: you can switch between different color palettes, or even define your own
 - supports both fixed and variable-width fonts
 
# Known issues
 - syntax highligthing of most languages - except C/C++ - is based on std::regex, which is diasppointingly slow. Because of that, the highlighting process is amortized between multiple frames. C/C++ has a hand-written tokenizer which is much faster. 
