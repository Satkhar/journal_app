#include "RemoteConnectionOptions.hpp"

#include <QHostAddress>
#include <QUrl>

namespace
{

constexpr int kMaximumRemoteTimeoutMs = 120000;
constexpr int kMaximumAuthTokenLength = 16384;

bool isLoopbackHost(const QString& host)
{
  if (host.compare("localhost", Qt::CaseInsensitive) == 0)
  {
    return true;
  }
  QHostAddress address;
  return address.setAddress(host) && address.isLoopback();
}

} // namespace

std::optional<RemoteConnectionOptions>
NormalizeRemoteConnectionOptions(RemoteConnectionOptions options,
                                 QString* errorMessage)
{
  auto fail = [errorMessage](const QString& error)
  {
    if (errorMessage)
    {
      *errorMessage = error;
    }
    return std::optional<RemoteConnectionOptions>();
  };
  if (errorMessage)
  {
    errorMessage->clear();
  }

  options.baseUrl = options.baseUrl.trimmed();
  options.authToken = options.authToken.trimmed();
  if (options.timeoutMs <= 0 || options.timeoutMs > kMaximumRemoteTimeoutMs)
  {
    return fail("Remote timeout is outside the supported range");
  }
  if (options.authToken.size() > kMaximumAuthTokenLength)
  {
    return fail("Remote auth token is too long");
  }
  for (const QChar character : options.authToken)
  {
    const ushort code = character.unicode();
    if (code < 0x21 || code > 0x7e)
    {
      return fail("Remote auth token contains an invalid character");
    }
  }

  QUrl url(options.baseUrl, QUrl::StrictMode);
  const QString scheme = url.scheme().toLower();
  if (!url.isValid() || url.isRelative() || url.host().isEmpty() ||
      (scheme != "http" && scheme != "https"))
  {
    return fail("Remote URL must be an absolute HTTP or HTTPS URL");
  }
  if (!url.userInfo().isEmpty() || url.hasQuery() || url.hasFragment())
  {
    return fail("Remote URL must not contain credentials, query or fragment");
  }
  if (scheme == "http" && !isLoopbackHost(url.host()) &&
      !options.allowInsecureHttp)
  {
    return fail("Plain HTTP is allowed only for a loopback server");
  }

  QString path = url.path();
  while (path.endsWith('/') && path.size() > 1)
  {
    path.chop(1);
  }
  if (path == "/")
  {
    path.clear();
  }
  url.setPath(path);
  options.baseUrl = url.toString(QUrl::FullyEncoded);
  return options;
}
