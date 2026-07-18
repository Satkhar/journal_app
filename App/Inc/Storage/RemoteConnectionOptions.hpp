#pragma once

#include <QString>

#include <optional>

struct RemoteConnectionOptions
{
  QString baseUrl;
  QString authToken;
  int timeoutMs{5000};
  bool allowInsecureHttp{false};
  // DDL/migrations выключены по умолчанию. Включать только для bootstrap
  // одноразового локального PoC, не для обычного desktop runtime.
  bool allowSchemaChanges{false};
};

// Нормализация выполняется до первого запроса. Credentials в URL запрещены:
// URL показывается в UI/logs, token передаётся только HTTP-заголовком.
std::optional<RemoteConnectionOptions>
NormalizeRemoteConnectionOptions(RemoteConnectionOptions options,
                                 QString* errorMessage = nullptr);
