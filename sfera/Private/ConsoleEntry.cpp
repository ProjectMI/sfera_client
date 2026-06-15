#include <windows.h>
#include <cstring>
#include <string>

extern "C" int sfera_client_winmain(HINSTANCE instance, HINSTANCE previousInstance, LPSTR commandLine, int showCommand);

namespace
{
    std::string buildWinMainCommandLine(int argc, char** argv)
    {
        const char* raw = GetCommandLineA();
        if (raw && *raw)
        {
            const char* cursor = raw;
            while (*cursor == ' ' || *cursor == '\t')
                ++cursor;
            if (*cursor == '"' || *cursor == '\'')
            {
                const char quote = *cursor++;
                while (*cursor && *cursor != quote)
                    ++cursor;
                if (*cursor == quote)
                    ++cursor;
            }
            else
            {
                while (*cursor && *cursor != ' ' && *cursor != '\t')
                    ++cursor;
            }
            while (*cursor == ' ' || *cursor == '\t')
                ++cursor;
            if (*cursor)
                return cursor;
        }
        std::string rebuilt;
        for (int index = 1; index < argc; ++index)
        {
            if (!rebuilt.empty())
                rebuilt.push_back(' ');
            const bool needsQuotes = std::strchr(argv[index], ' ') || std::strchr(argv[index], '\t');
            if (needsQuotes)
                rebuilt.push_back('"');
            rebuilt += argv[index];
            if (needsQuotes)
                rebuilt.push_back('"');
        }
        return rebuilt;
    }
}

int main(int argc, char** argv)
{
    std::string commandLine = buildWinMainCommandLine(argc, argv);
    char* commandLineBuffer = commandLine.empty() ? const_cast<char*>("") : &commandLine[0];
    return sfera_client_winmain(GetModuleHandleA(nullptr), nullptr, commandLineBuffer, SW_SHOWNORMAL);
}
