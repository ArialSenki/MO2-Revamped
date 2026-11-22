#ifndef COLLECTIONNAMES_H
#define COLLECTIONNAMES_H

// MO2 Revamped: the name and path helpers of a collection's install
// (collectionsession.cpp), on their own so they can be tested without the
// session. Qt Core and uibase's fixDirectoryName() only.

#include <QString>
#include <filesystem>

namespace Collections
{

// a folder name from the collection (a mod, profile or instance name): what
// fixDirectoryName leaves can still be "..", a device name or the original
// text when it gave up, and ".." as a mod name is the instance folder.
// False, with name emptied, when nothing safe is left
bool makeFolderName(QString& name);

// "1.6.1170.0" and "1.6.1170" are the same game: the first three numbers,
// without trailing zeros
QString versionKey(const QString& version);

// whether target, once resolved, is inside base
bool staysInside(const std::filesystem::path& base, const std::filesystem::path& target);

}  // namespace Collections

#endif  // COLLECTIONNAMES_H
