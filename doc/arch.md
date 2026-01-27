
## Описание архитектуры

Кратко о слоях и их ответственности:

- **UI (app_qt)** — Qt-интерфейс, экраны и виджеты, взаимодействие с пользователем.
- **APP (app)** — бизнес-логика и сценарии (use-cases): сервисы работы с журналом и синхронизации.
- **CORE (core)** — доменная модель и правила валидации; сериализация доменных объектов.
- **STORAGE (storage_local)** — локальное хранение: репозитории и их реализации.
- **CLIENT (client_sync)** — клиент синхронизации и интерфейс удалённого хранилища.
- **SERVER (server)** — HTTP-сервер и серверное хранилище.

Связи между слоями:

- UI вызывает сервисы уровня APP.
- APP работает с доменной моделью CORE и обращается к локальным репозиториям.
- Сервис синхронизации использует локальное хранилище, удалённый клиент и сериализацию.
- Клиент синхронизации общается с сервером по HTTP.


```mermaid
  flowchart TD

  subgraph UI["app_qt"]
    A[MainWindow]
    VM[ViewModel]
  end

  subgraph APP["app"]
    S[JournalService]
    SY[SyncService]
  end

  subgraph CORE["core"]
    M[Domain Model]
    V[Validation]
    SER[Serialization]
  end

  subgraph STORAGE["storage_local"]
    R1[IJournalRepository]
    L[LocalRepo]
  end

  subgraph CLIENT["client_sync"]
    API[RemoteClient]
    R2[IRemoteStore]
  end

  subgraph SERVER["server"]
    H[HTTP Server]
    ST[Server Storage]
  end

  A --> VM --> S
  VM --> SY

  S --> R1
  S --> M
  S --> V

  SY --> R1
  SY --> R2
  SY --> SER

  L -.-> R1
  API -.-> R2

  API <--> H
  H --> ST
  SER --> M
```


