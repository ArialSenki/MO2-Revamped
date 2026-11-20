#ifndef XMLREADER_H
#define XMLREADER_H

#include <QXmlStreamReader>

#include <stdexcept>

// the XML can't be read as it is (a wrong encoding, a broken tag): the
// dialog tries the other encodings before giving up
struct XmlParseError : std::runtime_error
{
  XmlParseError(const QString& message) : std::runtime_error(qUtf8Printable(message)) {}
};

class XmlReader : public QXmlStreamReader
{
public:
  XmlReader(QIODevice* device) : QXmlStreamReader(device) {}

  XmlReader(QByteArray array) : QXmlStreamReader(array) {}

  /** Get the next token, ignoring comments and white space text */
  TokenType readNext()
  {
    while (QXmlStreamReader::readNext() == Comment || isWhitespace()) {
      continue;
    }
    return tokenType();
  }

  /** get the next element.
   *
   * \param start - the name of the current start element
   *
   * \returns false if no more elements
   */
  bool getNextElement(QString const& start);

  /* Get the text associated with this token. */
  QString getText();

  /** Print a message if we get an unexpected tag */
  void unexpected();

  /** Read till the end of an element. Used for leaf nodes */
  void finishedElement();
};

#endif  // XMLREADER_H
