/*---------------------------------------------------------------------------*\
  =========                 |
  \\      /  F ield         | OpenFOAM: The Open Source CFD Toolbox
   \\    /   O peration     |
    \\  /    A nd           | Copyright (C) 2012 OpenFOAM Foundation
     \\/     M anipulation  |
-------------------------------------------------------------------------------
License
    This file is part of OpenFOAM.

    OpenFOAM is free software: you can redistribute it and/or modify it
    under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    OpenFOAM is distributed in the hope that it will be useful, but WITHOUT
    ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
    FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
    for more details.

    You should have received a copy of the GNU General Public License
    along with OpenFOAM.  If not, see <http://www.gnu.org/licenses/>.

Application
    foamDict

Description
    Query and modify OpenFOAM dictionary files.

    By default output is written to the standard output. Modifying operations
    (\a -set, \a -merge, \a -mergeSub, \a -remove, \a -changeKey, \a -clear)
    also output the resulting dictionary to standard output if not specified
    otherwise. For those operations this can be overriden by specifying
    \a -o or \a -inplace.

    The following gives a few examples for the usage of this tool:
    \verbatim
        $ foamDict -dict testDict -key solvers/p/solver -lookup
        PCG
        $ foamDict -dict testDict -key solvers/p/solver -set GAMG -inplace
        $ foamDict -dict testDict -key foo -lookup -default bar
        bar
        $ foamDict -dict testDict -key startFrom -found || echo OOOPS
        OOOPS
        $ foamDict -dict testDict -merge 'startFrom latestTime; endTime 1;' -i
        $ foamDict -dict testDict -key endTime -lookup
        1
        $ foamDict -dict testDict -key internalField -set 'uniform (0 0 0)'
    \endverbatim

Usage
    - foamDict [OPTIONS]

    \param -dict \<dictionary\> \n
    The dictionary file to operate on.

    \param -key \<key\> \n
    The dictionary entry to operate on.

    \param -toc \n
    Print the table of contents.

    \param -keys \n
    Return the list of available keys

    \param -patternKeys \n
    Return the list of available patterns

    \param -found \n
    Exits with 0 if \a \<key\> was found, 1 otherwise.

    \param -lookup \n
    Lookup the given key. Returns an error if the entry does not exist unless
    \a -default or \a -addDefault were specified.

    \param -set \<value\> \n
    Assign a new entry \a \<key\>, overwriting an existing entry.

    \param -merge \<string\> \n
    Merge with the dictionary specified in \a \<string\>. This is useful to
    perform multiple \a -set operations in a single invocation.

    \param -mergeSub \<mergeKey\> \n
    Merge with the dictionary named in \a \<mergeKey\>.

    \param -remove \n
    Remove the entry \a \<key\>.

    \param -changeKey \<newKey\> \n
    Change the keyword the \a \<key\> entry to \a \<newKey\>. \<newKey\> must
    be a single word.

    \param -clear \n
    Clear the dictionary specified by \a \<key\>.

    \param -default \<value\> \n
    Specify a default value for the \a -lookup operation.

    \param -addDefault \<value\> \n
    Specify a default value for the \a -lookup operation that will also be
    written to the dictionary if the entry does not exist. Implies \a -inplace.

    \param -o \<fileName\> \n
    Specify a file name for the output of the modifying operations. The
    querying operations silently ignore this option and output their result
    to standard output instead.

    \param -inplace \n
    Specify that the modifying operations should write back the output to the
    original file. The querying operations silently ignore this option and
    output their result to standard output instead (see \a -addDefault for the
    single exception).

    \param -i \n
    This is a short-cut for \a -inplace.

Author
    Michael Wild <wild@ifd.mavt.ethz.ch>

ToDo
    * Expose pattern matching.
    * Recursive key search.
    * Batch mode.
    * Interactive (readline) mode.

\*---------------------------------------------------------------------------*/

#include "argList.H"
#include "IFstream.H"
#include "OFstream.H"
#include "IOobject.H"
#include "dictionary.H"

using namespace Foam;

// * * * * * * * * * * * * * Local Helper Functions  * * * * * * * * * * * * //

#ifndef FOAM_HEX_VERSION
#error FOAM_HEX_VERSION not defined
#endif

namespace
{

// Helper to perform a dictionary::found() call with path keys
bool dictFound(const dictionary& dict, const fileName& key)
{
    bool result = false;
    wordList path = key.components();
    const dictionary* d = &dict;
    forAll(path, i)
    {
        if (!(result = d->found(path[i])))
        {
            break;
        }
        if (d->isDict(path[i]))
        {
            d = &d->subDict(path[i]);
        }
        else if (i+1 != path.size())
        {
            // Current key exists, but is a leaf and path not finished
            result = false;
            break;
        }
    }
    return result;
}


// Helper to perform a dictionary::subDict() call with path keys
dictionary& dictSubDict(dictionary& dict, const fileName& key)
{
    if (key == ".")
    {
        return dict;
    }
    dictionary* d = &dict;
    wordList path = key.components();
    forAll(path, i)
    {
        d = &d->subDict(path[i]);
    }
    return *d;
}


// Helper to write a decently formatted dictionary
void writeDict(Ostream& os, dictionary& dict)
{
    bool isFoamFile = dict.isDict("FoamFile");
    if (isFoamFile)
    {
        IOobject::writeBanner(os);
        os << "FoamFile";
        dict.subDict("FoamFile").write(os);
        IOobject::writeDivider(os);
        os << nl;
        dict.remove("FoamFile");
    }
    // Don't write in sub-dict syntax
    dict.write(os, false);
    if (isFoamFile)
    {
        IOobject::writeEndDivider(os);
    }
}


// Helper to read a dictionary from file including the header
void dictRead(dictionary& dict, Istream& is)
{
#if FOAM_HEX_VERSION < 0x200
    // This is a simple copy-paste from the 1.7.x dictionary::read(Istream&)
    // sans the erase("FoamFile") invocation.
    if (!is.good())
    {
        FatalIOErrorIn("dictRead(dictionary&, Istream&)", is)
            << "Istream not OK for reading dictionary "
            << exit(FatalIOError);
        return;
    }

    token currToken(is);
    if (currToken != token::BEGIN_BLOCK)
    {
        is.putBack(currToken);
    }

    while (!is.eof() && entry::New(dict, is))
    {}

    if (is.bad())
    {
        Info<< "dictRead(dictionary&, Istream&) : "
            << "Istream not OK after reading dictionary " << dict.name()
            << endl;
    }
#else
    (void)dict.read(is, true);
#endif
}


// Helper to write only the contents of a primitiveEntry
void writePrimitiveEntry(const primitiveEntry& pe, Ostream& os)
{
#if FOAM_HEX_VERSION < 0x200
    // This is a straight copy from primitiveEntry::write(Ostream&) sans the
    // keyword and END_STATEMENT
    for (label i=0; i<pe.size(); i++)
    {
        os << pe[i];

        if (i < pe.size()-1)
        {
            os << token::SPACE;
        }
    }
#else
    pe.write(os, true);
#endif
}


// Helper macros for adding options dependent on the OpenFOAM version
#if FOAM_HEX_VERSION < 0x200
#define ADD_NOTE(NOTE)
#define ADD_OPTION(NAME, META, DOC) argList::validOptions.insert(NAME, META)
#define ADD_BOOL_OPTION(NAME, DOC) argList::validOptions.insert(NAME, "")
#define REMOVE_OPTION(NAME) argList::validOptions.erase(NAME)
#else
#define ADD_NOTE(NOTE) argList::addNote(NOTE)
#define ADD_OPTION(NAME, META, DOC) argList::addOption(NAME, META, DOC)
#define ADD_BOOL_OPTION(NAME, DOC) argList::addBoolOption(NAME, DOC)
#define REMOVE_OPTION(NAME) argList::removeOption(NAME)
#endif


// Helper to set up the options
void initArgList()
{
    ADD_NOTE
    (
        "Query and modify OpenFOAM dictionary files. When modifying the\n"
        "dictionary, this utility *REMOVES* comments. By default output is\n"
        "written to the standard output. Modifying operations (-set, -merge,\n"
        "-mergeSub, -remove, -changeKey, -clear) also output the resulting\n"
        "dictionary to standard output if not specified otherwise. For those\n"
        "operations this can be overriden by specifying -o or -inplace.\n"
        "Keys can be of the form <parent>/<sub>/<entry>."
    );

    ADD_OPTION
    (
        "dict",
        "dictionary",
        "The dictionary to operate on."
    );

    ADD_OPTION
    (
        "key",
        "key",
        "The dictionary entry to operate on."
    );

    ADD_BOOL_OPTION
    (
        "toc",
        "Print the table of contents."
    );

    ADD_BOOL_OPTION
    (
        "keys",
        "Return the list of available keys."
    );

    ADD_BOOL_OPTION
    (
        "patternKeys",
        "Return the list of available patterns."
    );

    ADD_BOOL_OPTION
    (
        "found",
        "Exits with 0 if <key> was found, 1 otherwise."
    );

    ADD_BOOL_OPTION
    (
        "lookup",
        "Lookup the given key. Returns an error if the entry does not exist."
    );

    ADD_OPTION
    (
        "set",
        "value",
        "Assign a new entry <key>, overwriting an existing entry."
    );

    ADD_OPTION
    (
        "merge",
        "string",
        "Merge with the dictionary specified in <string>. This is useful to "
        "perform multiple -set operations in a single invocation."
    );

    ADD_OPTION
    (
        "mergeSub",
        "mergeKey",
        "Merge with the dictionary named in <mergeKey>."
    );

    ADD_BOOL_OPTION
    (
        "remove",
        "Remove the <key> entry."
    );

    ADD_OPTION
    (
        "changeKey",
        "newKey",
        "Change the keyword for the <key> entry. <newKey> must be a single "
        "word."
    );

    ADD_BOOL_OPTION
    (
        "clear",
        "Clear the dictionary specified by <key>."
    );

    ADD_OPTION
    (
        "default",
        "value",
        "Specify a default value for the -lookup operation."
    );

    ADD_OPTION
    (
        "addDefault",
        "value",
        "Specify a default value for the -lookup operation that will also "
        "be written to the dictionary if the entry does not exist."
    );

    ADD_OPTION
    (
        "o",
        "fileName",
        "Specify a file name for the output of the modifying operations. The "
        "querying operations silently ignore this option and output their "
        "result to standard output instead."
    );

    ADD_BOOL_OPTION
    (
        "inplace",
        "Specify that the modifying operations should write back the output "
        "to the original file. The querying operations silently ignore this "
        "option and output their result to standard output instead (see "
        "-addDefault for the single exception)."
    );

    ADD_BOOL_OPTION
    (
        "i",
        "This is short for -inplace."
    );

    argList::noBanner();
    argList::noParallel();
    REMOVE_OPTION("case");
    REMOVE_OPTION("noFunctionObjects");
}

} // End anonymous namespace


// * * * * * * * * * * * * * * *  Main Program * * * * * * * * * * * * * * * //


int main(int argc, char *argv[])
{
    initArgList();
    argList args(argc, argv);

#if FOAM_HEX_VERSION < 0x200
    // Make the arguments of these options strings in OpenFOAM terms
    const char* stringOptsArray[] =
    {
        "dict",
        "key",
        "mergeSub",
        "changeKey",
        "o"
    };
    UList<const char*> stringOpts
    (
        stringOptsArray,
        sizeof(stringOptsArray)/sizeof(char*)
    );
    forAll(stringOpts, i)
    {
        word opt = stringOpts[i];
        if (args.optionFound(opt)
         && args.options()[opt][0] != '"')
        {
            // THIS IS A HACK
            string& s = const_cast<string&>(args.options()[opt]);
            s = "\"" + s + "\"";
        }
    }
#endif

    // List and enum of available operations
    const char* operationsArray[] =
    {
        "toc",
        "keys",
        "patternKeys",
        "found",
        "lookup",
        "set",
        "merge",
        "mergeSub",
        "remove",
        "changeKey",
        "clear"
    };
    UList<const char*> operations
    (
        operationsArray,
        sizeof(operationsArray)/sizeof(char*)
    );
    enum Operation
    {
        OPERATION_NONE=-1,
        OPERATION_TOC,
        OPERATION_KEYS,
        OPERATION_PATTERNKEYS,
        OPERATION_FOUND,
        OPERATION_LOOKUP,
        OPERATION_SET,
        OPERATION_MERGE,
        OPERATION_MERGESUB,
        OPERATION_REMOVE,
        OPERATION_CHANGEKEY,
        OPERATION_CLEAR
    };

    // Require and operation
    Operation op = OPERATION_NONE;
    word opName = word::null;
    forAll(operations, i)
    {
        if (args.optionFound(operations[i]))
        {
            if (op != OPERATION_NONE)
            {
                FatalErrorIn("main(int, char**)")
                    << "Multiple operations specified\n"
                    << exit(FatalError);
            }
            op = Operation(i);
            opName = operations[i];
        }
    };

    // Only allow a single operation
    switch(op)
    {
        case OPERATION_FOUND:
        case OPERATION_LOOKUP:
        case OPERATION_SET:
        case OPERATION_REMOVE:
        case OPERATION_CHANGEKEY:
            if (!args.optionFound("key"))
            {
                FatalErrorIn("main(int, char**)")
                    << "The " << opName << " operation requires -key <key> "
                    << "to be specified.\n"
                    << exit(FatalError);
            }
        default:
            break;
    }

    // Read dictionary
    if (!args.optionFound("dict"))
    {
        FatalErrorIn("main(int, char**)")
            << "Missing -dict option.\n"
            << exit(FatalError);
    }
    fileName fName = args.optionRead<fileName>("dict");
    dictionary dict(fName);
    {
        IFstream ifs(fName);
        // Read including the header
        dictRead(dict, ifs);
    }

    // Prepare output stream
    autoPtr<Ostream> outFileStream;
    switch(op)
    {
        case OPERATION_LOOKUP:
            if (args.optionFound("addDefault"))
            {
                outFileStream.reset(new OFstream(fName));
            }
            break;
        case OPERATION_SET:
        case OPERATION_MERGE:
        case OPERATION_MERGESUB:
        case OPERATION_REMOVE:
        case OPERATION_CHANGEKEY:
        case OPERATION_CLEAR:
        {
            fileName outName = fileName::null;
            if (args.optionFound("i") || args.optionFound("inplace"))
            {
                outName = fName;
            }
            else if (args.optionFound("o"))
            {
                outName = args.option("o");
            }
            if (outName != fileName::null)
            {
                outFileStream.reset(new OFstream(outName));
            }
            break;
        }
        default:
            break;
    }
    Ostream& os = outFileStream.valid() ? outFileStream() : Info;

    // Perform actual operations
    fileName key = fileName::null;
    (void) args.optionReadIfPresent("key", key);
    switch(op)
    {
        case OPERATION_NONE:
            FatalErrorIn("main(int, char**)")
                << "No operation specified.\n"
                << exit(FatalError);
            break;

        case OPERATION_TOC:
            Info<< dict.toc();
            break;

        case OPERATION_KEYS:
        case OPERATION_PATTERNKEYS:
            Info<< dict.keys(op==OPERATION_PATTERNKEYS);
            break;

        case OPERATION_FOUND:
            return !dictFound(dict, key);
            break;

        case OPERATION_LOOKUP:
        {
            string defValue = string::null;
            args.optionReadIfPresent("default", defValue);
            string defAddValue = string::null;
            args.optionReadIfPresent("addDefault", defAddValue);
            if (defValue != string::null && defAddValue != string::null)
            {
                FatalErrorIn("main(int, argc**)")
                    << "Options -default and -addDefault are mutually "
                    << "exclusive.\n" << exit(FatalError);
            }
            if (!dictFound(dict, key))
            {
                if (defValue == string::null && defAddValue == string::null)
                {
                    // Trigger the fatal error for the non-existing key
                    static_cast<void>(dict.lookup(key));
                }
                else if(defValue != string::null)
                {
                    Info<< defValue << nl;
                }
                else
                {
                    // Construct the merge-dictionary entry for the default
                    // value. Do it this way such that new sub-dictionaries
                    // are created on a as-needed basis (similar to `mkdir -p`)
                    string start = "{";
                    string end = "}";
                    if (key.path() != ".")
                    {
                        wordList path = key.path().components();
                        forAll(path, i)
                        {
                            start += path[i] + " {";
                            end += "}";
                        }
                    }
                    start += key.name() + " " + defAddValue + ";";
                    IStringStream iss(start+end);
                    dict.merge(dictionary(iss));
                    writeDict(os, dict);
                    Info<< defAddValue << nl;
                }
                break;
            }
            fileName parentKey = key.path();
            fileName childKey = key.name();
            const dictionary& d = dictSubDict(dict, parentKey);
            if (d.isDict(childKey))
            {
                Info<< d.subDict(childKey) << nl;
            }
            else
            {
                // We know this is a primitive entry, so we don't want the
                // ugly formatting of a ITstream.
                primitiveEntry pe(childKey, d.lookup(childKey));
                writePrimitiveEntry(pe, Info);
                Info<< nl;
            }
            break;
        }

        case OPERATION_SET:
        {
            IStringStream iss(key.name() + " " + args.option("set") + ";\n");
            dictionary& d = dictSubDict(dict, key.path());
            d.set(entry::New(iss));
            writeDict(os, dict);
            break;
        }

        case OPERATION_MERGE:
        {
            dictionary& d = args.optionFound("key")
                ? dictSubDict(dict, key)
                : dict;
            IStringStream iss = args.optionLookup("merge");
            d.merge(dictionary(iss));
            writeDict(os, dict);
            break;
        }

        case OPERATION_MERGESUB:
        {
            fileName mergeKey = args.option("mergeSub");
            if (!dictFound(dict, mergeKey))
            {
                FatalErrorIn("main(int, char**)")
                    << "Key specified in -mergeKey does not exist.\n"
                    << exit(FatalError);
            }
            dictionary& dst = args.optionFound("key")
                ? dictSubDict(dict, key)
                : dict;
            dictionary& src = dictSubDict(dict, mergeKey);
            dst.merge(src);
            writeDict(os, dict);
            break;
        }

        case OPERATION_REMOVE:
            if (dictFound(dict, key))
            {
                dictSubDict(dict, key.path()).remove(key.name());
                writeDict(os, dict);
            }
            break;

        case OPERATION_CHANGEKEY:
            if (dictFound(dict, key))
            {
                word changeKey = args.optionRead<word>("changeKey");
                dictionary& d = dictSubDict(dict, key.path());
                d.changeKeyword(key.name(), changeKey);
                writeDict(os, dict);
            }
            else
            {
                FatalErrorIn("main(int, char**)")
                    << "The key " << key << " does not exist.\n"
                    << exit(FatalError);
            }

            break;

        case OPERATION_CLEAR:
            if (key == fileName::null)
            {
                dict.clear();
            }
            else
            {
                if (!dictFound(dict, key))
                {
                    FatalErrorIn("main(int, char**")
                        << "The key " << key << " does not exist.\n"
                        << exit(FatalError);
                }

                dictionary& d = dictSubDict(dict, key.path());
                if (d.isDict(key.name()))
                {
                    d.subDict(key.name()).clear();
                }
                else
                {
                    FatalErrorIn("main(int, char**)")
                        << "The key " << key << " does not name a "
                        << "sub-dictionary.\n" << exit(FatalError);
                    break;
                }
            }
            writeDict(os, dict);
            break;

        default:
            FatalErrorIn("main(int, char**)")
                << "This should not happen!\n"
                << abort(FatalError);
    }

    return 0;
}


// ************************************************************************* //
