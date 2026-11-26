#include "pch.h"
#include "IVText.h"
#include <vcclr.h>
#include <unordered_map>
#include <string>

namespace
{
    // Label strings are packed back to back in the game's text table, so the room a label has is the length of
    // its original text. That is recorded on the first write (a shorter replacement must not shrink it), together
    // with the text we wrote: if the label no longer holds that text, the game has reloaded its text table
    // (e.g. after an episode switch) and the capacity is measured again.
    struct LabelInfo
    {
        size_t capacity;            // in wchar_t, including the terminator
        std::wstring lastWritten;
    };
    std::unordered_map<const wchar_t*, LabelInfo> s_labels;
    SRWLOCK s_labelsLock = SRWLOCK_INIT;
}

namespace IVSDKDotNet
{
    // Copies newText into the label's existing buffer without ever writing past it. Writing more than the
    // label's original length overwrites the following labels (garbled text, possible crashes), so longer
    // text is truncated with a warning. Mods that need long text should ship a GXT with long placeholders.
    void IVText::WriteLabelText(wchar_t* label, String^ newText, String^ labelName)
    {
        if (!label)
            return;

        pin_ptr<const wchar_t> src = PtrToStringChars(newText);
        size_t length = (size_t)newText->Length;
        size_t capacity;
        bool truncated = false;

        AcquireSRWLockExclusive(&s_labelsLock);
        {
            auto it = s_labels.find(label);
            if (it == s_labels.end() || it->second.lastWritten != label)
                it = s_labels.insert_or_assign(label, LabelInfo{ wcslen(label) + 1, std::wstring() }).first;

            capacity = it->second.capacity;
            if (length + 1 > capacity)
            {
                length = capacity - 1;
                truncated = true;
            }

            wmemcpy(label, src, length);
            label[length] = L'\0';
            it->second.lastWritten.assign(label, length);
        }
        ReleaseSRWLockExclusive(&s_labelsLock);

        if (truncated)
            Logger::LogWarning(String::Format("[IVText] Text for label {0} is {1} characters long but the label only has room for {2}. The text was truncated.", labelName, newText->Length, capacity - 1));
    }

    // - - - Constructor - - -
    IVText::IVText(CText* nativePtr)
    {
        NULLPTR_CHECK(nativePtr);
        NativeText = nativePtr;
    }

    // - - - Methods / Functions - - -
    String^ IVText::Get(String^ ident)
    {
        NULLPTR_CHECK_WITH_RETURN(NativeText, String::Empty);
        CHECK_NULL_OR_WHITESPACE_STRING_WITH_RETURN(ident, String::Empty);

        msclr::interop::marshal_context ctx;
        return gcnew String(NativeText->Get(ctx.marshal_as<const char*>(ident)));
    }
    String^ IVText::Get(uint32_t hash, String^ ident)
    {
        NULLPTR_CHECK_WITH_RETURN(NativeText, String::Empty);
        CHECK_NULL_OR_WHITESPACE_STRING_WITH_RETURN(ident, String::Empty);

        msclr::interop::marshal_context ctx;
        return gcnew String(NativeText->Get(hash, ctx.marshal_as<const char*>(ident)));
    }

    void IVText::ReplaceTextOfTextLabel(String^ ident, String^ newText)
    {
        NULLPTR_CHECK(NativeText);
        NULLPTR_CHECK(ident);
        NULLPTR_CHECK(newText);

        msclr::interop::marshal_context ctx;
        WriteLabelText(const_cast<wchar_t*>(NativeText->Get(ctx.marshal_as<const char*>(ident))), newText, ident);
    }
    void IVText::ReplaceTextOfTextLabel(uint32_t hash, String^ newText)
    {
        NULLPTR_CHECK(NativeText);
        NULLPTR_CHECK(newText);

        WriteLabelText(const_cast<wchar_t*>(NativeText->Get(hash, NULL)), newText, hash.ToString("X8"));
    }

}