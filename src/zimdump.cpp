/*
 * Copyright (C) 2006 Tommi Maekitalo
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * is provided AS IS, WITHOUT ANY WARRANTY; without even the implied
 * warranty of MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE, and
 * NON-INFRINGEMENT.  See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 *
 */

#include <iostream>
#include <sstream>
#include <fstream>
#include <set>
#include <zim/file.h>
#include <zim/fileiterator.h>
#include <stdexcept>
#include <sys/types.h>
#include <docopt/docopt.h>
#include <sys/stat.h>
#include <iomanip>
#include <array>
#include <vector>
#include <codecvt>

#include "arg.h"
#include "version.h"

#include <fcntl.h>
#ifdef _WIN32
# define SEPARATOR "\\"
# include <io.h>
# include <windows.h>
#else
# define SEPARATOR "/"
#include <unistd.h>
#endif

#define ERRORSDIR "_exceptions/"

std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;

inline static void createdir(const std::string &path, const std::string &base)
{
    if (path.size() <= 1)
        return ;

    std::size_t position = 0;
    while(position != std::string::npos)
    {
        position = path.find('/', position+1);
        if (position != std::string::npos)
        {
            std::string fulldir = base + path.substr(0, position);
            #if defined(_WIN32)
            std::wstring wfulldir = converter.from_bytes(fulldir);
            CreateDirectoryW(wfulldir.c_str(), NULL);
            #else
              ::mkdir(fulldir.c_str(), 0777);
            #endif
        }
    }
}

static bool isReservedUrlChar(const char c)
{
    constexpr std::array<char, 10> reserved = {{';', ',', '?', ':',
                                               '@', '&', '=', '+', '$' }};

    return std::any_of(reserved.begin(), reserved.end(),
                       [&c] (const char &elem) { return elem == c; } );
}

static bool needsEscape(const char c, const bool encodeReserved)
{
  if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))
    return false;

  if (isReservedUrlChar(c))
    return encodeReserved;

  constexpr std::array<char, 10> noNeedEscape = {{'-', '_', '.', '!', '~',
                                                '*', '\'', '(', ')', '/' }};

  return not std::any_of(noNeedEscape.begin(), noNeedEscape.end(),
                         [&c] (const char &elem) { return elem == c; } );
}

std::string urlEncode(const std::string& value, bool encodeReserved)
{
  std::ostringstream os;
  os << std::hex << std::uppercase;
  for (std::string::const_iterator it = value.begin();
       it != value.end();
       ++it) {
    if (!needsEscape(*it, encodeReserved)) {
      os << *it;
    } else {
      os << '%' << std::setw(2) << static_cast<unsigned int>(static_cast<unsigned char>(*it));
    }
  }
  return os.str();
}

class ZimDumper
{
    zim::File file;
    zim::File::const_iterator pos;
    bool verbose;

  public:
    ZimDumper(const std::string& fname)
      : file(fname),
        pos(file.begin()),
        verbose(false)
      { }

    void setVerbose(bool sw = true)  { verbose = sw; }

    void printInfo();
    void printNsInfo(char ch);
    void locateArticle(zim::size_type idx);
    void findArticle(char ns, const char* expr, bool title);
    void findArticleByUrl(const std::string& url);
    void dumpArticle();
    void printPage();
    void listArticles(bool info, bool extra);
    void listArticle(const zim::Article& article, bool extra);
    void listArticleT(const zim::Article& article, bool extra);
    void listArticle(bool extra)
      { listArticle(*pos, extra); }
    void listArticleT(bool extra)
      { listArticleT(*pos, extra); }
    void dumpFiles(const std::string& directory, bool symlinkdump, std::function<bool (const char c)> nsfilter);
};

void ZimDumper::printInfo()
{
  std::cout << "count-articles: " << file.getCountArticles() << "\n";
  if (verbose)
  {
    std::string ns = file.getNamespaces();
    std::cout << "namespaces: " << ns << '\n';
    for (std::string::const_iterator it = ns.begin(); it != ns.end(); ++it)
      std::cout << "namespace " << *it << " size: " << file.getNamespaceCount(*it) << '\n';
  }
  std::cout << "uuid: " << file.getFileheader().getUuid() << "\n"
               "article count: " << file.getFileheader().getArticleCount() << "\n"
               "mime list pos: " << file.getFileheader().getMimeListPos() << "\n"
               "url ptr pos: " << file.getFileheader().getUrlPtrPos() << "\n"
               "title idx pos: " << file.getFileheader().getTitleIdxPos() << "\n"
               "cluster count: " << file.getFileheader().getClusterCount() << "\n"
               "cluster ptr pos: " << file.getFileheader().getClusterPtrPos() << "\n";
  if (file.getFileheader().hasChecksum())
    std::cout <<
               "checksum pos: " << file.getFileheader().getChecksumPos() << "\n"
               "checksum: " << file.getChecksum() << "\n";
  else
    std::cout <<
               "no checksum\n";

  if (file.getFileheader().hasMainPage())
    std::cout << "main page: " << file.getFileheader().getMainPage() << "\n";
  else
    std::cout << "main page: " << "-\n";

  if (file.getFileheader().hasLayoutPage())
    std::cout << "layout page: " << file.getFileheader().getLayoutPage() << "\n";
  else
    std::cout << "layout page: " << "-\n";

  std::cout.flush();
}

void ZimDumper::printNsInfo(char ch)
{
  std::cout << "namespace " << ch << "\n"
               "lower bound idx: " << file.getNamespaceBeginOffset(ch) << "\n"
               "upper bound idx: " << file.getNamespaceEndOffset(ch) << std::endl;
}

void ZimDumper::locateArticle(zim::size_type idx)
{
  pos = zim::File::const_iterator(&file, idx, zim::File::const_iterator::UrlIterator);
}

void ZimDumper::findArticle(char ns, const char* expr, bool title)
{
  if (title)
    pos = file.findByTitle(ns, expr);
  else
    pos = file.find(ns, expr);
}

void ZimDumper::findArticleByUrl(const std::string& url)
{
    pos = file.find(url);
}

void ZimDumper::printPage()
{
  if(pos!=file.end())
  {
    std::cout << pos->getPage() << std::flush;
  }
}

void ZimDumper::dumpArticle()
{
  if(pos!=file.end())
  {
    std::cout << pos->getData() << std::flush;
  }
}

void ZimDumper::listArticles(bool info, bool extra)
{
  for (zim::File::const_iterator it = pos; it != file.end(); ++it)
  {
    if (info)
      listArticle(*it, extra);
    else
        std::cout << it->getUrl() << '\n';
  }
}

void ZimDumper::listArticle(const zim::Article& article, bool extra)
{
  std::cout <<
      "url: "             << article.getUrl() << "\n"
    "\ttitle:           " << article.getTitle() << "\n"
    "\tidx:             " << article.getIndex() << "\n"
    "\tnamespace:       " << article.getNamespace() << "\n"
    "\ttype:            " << (article.isRedirect()   ? "redirect"
                            : article.isLinktarget() ? "linktarget"
                            : article.isDeleted()    ? "deleted"
                            :                         "article") << "\n";

  if (article.isRedirect())
  {
    std::cout <<
      "\tredirect index:  " << article.getRedirectIndex() << "\n";
  }
  else if (article.isLinktarget())
  {
    // nothing else
  }
  else if (article.isDeleted())
  {
    // nothing else
  }
  else
  {
    std::cout <<
      "\tmime-type:       " << article.getMimeType() << "\n"
      "\tarticle size:    " << article.getArticleSize() << "\n";
  }

  if (extra)
  {
    std::string parameter = article.getParameter();
    std::cout << "\textra:           ";
    static char hexdigit[] = "0123456789abcdef";
    for (std::string::const_iterator it = parameter.begin(); it != parameter.end(); ++it)
    {
      unsigned val = static_cast<unsigned>(static_cast<unsigned char>(*it));
      std::cout << hexdigit[val >> 4] << hexdigit[val & 0xf] << ' ';
    }
    std::cout << std::endl;
  }
}

void ZimDumper::listArticleT(const zim::Article& article, bool extra)
{
  std::cout << article.getNamespace()
    << '\t' << article.getUrl()
    << '\t' << article.getTitle()
    << '\t' << article.getIndex()
    << '\t' << (article.isRedirect()   ? 'R'
              : article.isLinktarget() ? 'L'
              : article.isDeleted()    ? 'D'
              :                         'A');

  if (article.isRedirect())
  {
    std::cout << '\t' << article.getRedirectIndex();
  }
  else if (article.isLinktarget())
  {
    // nothing else
  }
  else if (article.isDeleted())
  {
    // nothing else
  }
  else
  {
    std::cout << '\t' << article.getMimeType()
              << '\t' << article.getArticleSize();
  }

  if (extra)
  {
    std::string parameter = article.getParameter();
    std::cout << '\t';
    static char hexdigit[] = "0123456789abcdef";
    for (std::string::const_iterator it = parameter.begin(); it != parameter.end(); ++it)
    {
      unsigned val = static_cast<unsigned>(static_cast<unsigned char>(*it));
      std::cout << hexdigit[val >> 4] << hexdigit[val & 0xf] << '\t';
    }
  }
  std::cout << std::endl;
}
void write_to_error_directory(const std::string& base, const std::string relpath, const char *content, ssize_t size)
{
    createdir(ERRORSDIR, base);
    std::string url = relpath;

    std::string::size_type p;
    while ((p = url.find('/')) != std::string::npos)
        url.replace(p, 1, "%2f");

#ifdef _WIN32
    auto fullpath = std::string(base + ERRORSDIR + url);
    std::wstring wpath = converter.from_bytes(fullpath);
    auto fd = _wopen(wpath.c_str(), _O_WRONLY | _O_CREAT | _O_TRUNC, S_IWRITE);

    if (fd == -1) {
        std::cerr << "Error opening file " + fullpath + " cause: " + ::strerror(errno) << std::endl;
        return ;
    }
    if (write(fd, content, size) != size) {
      close(fd);
      std::cerr << "Failed writing: " << fullpath << " - " << ::strerror(errno) << std::endl;
    }
#else
    std::ofstream stream(base + ERRORSDIR + url);

    stream.write(content, size);

    if (stream.fail() || stream.bad())
    {
        std::cerr << "Error writing file to errors dir. " << (base + ERRORSDIR + url) << std::endl;
    }else {
        std::cerr << "Wrote " << (base + relpath) << " to " << (base + ERRORSDIR + url) << std::endl;
    }
#endif
}

inline void write_to_file(const std::string &base, const std::string& path, const char* data, ssize_t size) {
    std::string fullpath = base + path;
#ifdef _WIN32
    std::wstring wpath = converter.from_bytes(fullpath);
    auto fd = _wopen(wpath.c_str(), _O_WRONLY | _O_CREAT | _O_TRUNC, S_IWRITE);
#else
    auto fd = open(fullpath.c_str(), O_WRONLY | O_CREAT | O_TRUNC,
                              S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
#endif
    if (fd == -1) {
        write_to_error_directory(base, path, data, size);
        return ;
    }
    if (write(fd, data, size) != size) {
      write_to_error_directory(base, path, data, size);
    }
    close(fd);
}


void ZimDumper::dumpFiles(const std::string& directory, bool symlinkdump, std::function<bool (const char c)> nsfilter)
{
  unsigned int truncatedFiles = 0;
#if defined(_WIN32)
    std::wstring wdir = converter.from_bytes(directory);
    CreateDirectoryW(wdir.c_str(), NULL);
#else
  ::mkdir(directory.c_str(), 0777);
#endif

  std::vector<std::string> pathcache;
  std::set<char> nscache;
  for (zim::File::const_iterator it = pos; it != file.end(); ++it)
  {
    char filenamespace = it->getNamespace();
    if (nsfilter(it->getNamespace()))
        continue;

    {
    #if defined(_WIN32)
        std::wstring wbase = converter.from_bytes(base);
        CreateDirectoryW(wbase.c_str(), NULL);
    #else
      ::mkdir(base.c_str(), 0777);
    #endif
        nscache.insert(it->getNamespace());
    }
    std::string t = it->getTitle();
    std::string url = it->getUrl();

    auto position = url.find_last_of('/');
    if (position != std::string::npos)
    {
        std::string path = url.substr(0, position);
        if (find(pathcache.begin(), pathcache.end(), path) == pathcache.end())
        {
            createdir(url, base);
            pathcache.push_back(path);
        }
    }

    if ( t.length() > 255 )
    {
        std::ostringstream sspostfix, sst;
        sspostfix << (++truncatedFiles);
        sst << url.substr(0, 254-sspostfix.tellp()) << "~" << sspostfix.str();
        url = sst.str();
    }

    std::stringstream ss;
    ss << filenamespace;
    ss << SEPARATOR + url;
    std::string relative_path = ss.str();
    std::string full_path = directory + SEPARATOR + relative_path;

    if (it->isRedirect())
    {
        auto redirectArticle = it->getRedirectArticle();
        std::string redirectUrl = redirectArticle.getUrl();
        if (symlinkdump == false && redirectArticle.getMimeType() == "text/html")
        {
            auto encodedurl = urlEncode(redirectUrl, true);
            std::ostringstream ss;

            ss << "<!DOCTYPE html><head><meta http-equiv=\"Content-Type\" content=\"text/html; charset=utf-8\" />";
            ss << "<meta http-equiv=\"refresh\" content=\"0;url=" + encodedurl + "\" /><head><body></body></html>";
            auto content = ss.str();
            write_to_file(directory + SEPARATOR, relative_path, content.c_str(), content.size());
        } else {
#ifdef _WIN32
            auto blob = redirectArticle.getData();
            write_to_file(directory + SEPARATOR, relative_path, blob.data(), blob.size());
#else
            if (symlink(redirectUrl.c_str(), full_path.c_str()) != 0) {
              throw std::runtime_error(
                std::string("Error creating symlink from ") + redirectUrl + " to " + full_path);
            }
#endif
        }
    } else {
      auto blob = it->getData();
      write_to_file(directory + SEPARATOR, relative_path, blob.data(), blob.size());
    }
  }
}

static const char USAGE[] =
R"(zimdump.
#ifndef _WIN32
                   "  -s        Use symlink to dump html redirect. Else create html redirect file."
#endif

    Usage:
      zimdump <zim_file> info [--ns=N]
      zimdump <zim_file> list [--details] [--idx=INDEX|(--url=URL [--ns=N])]
      zimdump <zim_file> dump --dir=DIR [--ns=N] [--redirect]
      zimdump <zim_file> dump (--idx=INDEX |(--url=URL [--ns=N]))

    Options:
      --idx=INDEX       The index of the article to list/dump
      --ns=N            Namespace selection [default: A]
      -h --help         Show this screen.
      --version         Show version.
)";

void subcmdInfo(ZimDumper &app, std::map<std::string, docopt::value> &args)
{
    if (args["--namespaceinfo"]) {
        app.printNsInfo(args["--namespaceinfo"].asString().at(0));
    }
    else {
        app.printInfo();
    }
}

void subcmdDumpAll(ZimDumper &app, std::map<std::string, docopt::value> &args, bool redirect, std::function<bool (const char c)> nsfilter)
{
#ifdef _WIN32
    app.dumpFiles(args["--dir"].asString(), false, nsfilter);
#else
    app.dumpFiles(args["--dir"].asString(), redirect, nsfilter);
#endif
}


void subcmdDump(ZimDumper &app,  std::map<std::string, docopt::value> &args)
{
    bool redirect = false;

    if (args["--redirect"]) redirect = true;

    if (args["--dir"]) {
        std::function<bool (const char c)> filter = [](const char /*c*/){return false; };
        if (args["--ns"])
        {
            std::string nspace = args["--ns"].asString();
            filter = [nspace](const char c){ return nspace.at(0) != c; };
        }
        return subcmdDumpAll(app, args, redirect, filter);
    }

    if (args["--idx"])
    {
        app.locateArticle(args["--idx"].asLong());
    } else if(args["--url"])
    {
        std::string nspace = "A";
        if (args["--ns"])
            nspace = args["--ns"].asString();
        app.findArticleByUrl(nspace + "/" + args["--url"].asString());
    }
    app.dumpArticle();
}

void subcmdList(ZimDumper &app, std::map<std::string, docopt::value> &args)
{
    bool idx = false;
    bool url = false;
    bool details = args["--details"].asBool();

    if (args["--idx"]) idx = true;
    if (args["--url"]) url = true;

    if (idx || url)
    {
        if (idx)
        {
            app.locateArticle(args["--idx"].asLong());
        } else if(url)
        {
            std::string nspace = "A";
            if (args["--ns"])
                nspace = args["--ns"].asString();
            app.findArticleByUrl(nspace + "/" + args["--url"].asString());
        }
        app.listArticle(details);
    }
    else
    {
        app.listArticles(details, details);
    }
}

int main(int argc, char* argv[])
{
    std::map<std::string, docopt::value> args
        = docopt::docopt(USAGE,
                         { argv + 1, argv + argc },
                         true,
                         "zimdump 1.0");

    try {
        ZimDumper app(args["<zim_file>"].asString());

        app.setVerbose(args["--verbose"].asBool());

        std::unordered_map<std::string, std::function<void(ZimDumper&, decltype(args)&)>> dispatchtable = {
            {"info",            subcmdInfo },
            {"dump",            subcmdDump },
            {"list",            subcmdList }
        };

        // call the appropriate subcommand handler
        for (const auto &it : dispatchtable) {
            if (args[it.first.c_str()].asBool()) {
                (it.second)(app, args);
                break;
            }
        }
    } catch (std::exception &e) {
        std::cout << "Exception: " << e.what() << '\n';
    }
    return 0;
}
