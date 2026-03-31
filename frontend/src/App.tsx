import { useCallback, useEffect, useMemo, useRef, useState, type RefObject } from 'react'
import './App.css'

type AppView = 'pager' | 'water-pump'

type AudioRecord = {
  id: string
  deviceName: string
  createdAt: string
}

type PumpSnapshot = {
  deviceName: string
  state: string
  temp: string
}

type BridgeMessage = {
  topic?: string
  payload?: string
  timestamp?: number
  subscribed?: string
  error?: string
}

const PAGER_HASH = '#pager'
const WATER_PUMP_HASH = '#water-pump'
const MAX_STATUS_LINES = 150
const DEFAULT_STATUS_LINE = 'Status goes here. Maximum 150 lines'
const PUMP_DEVICE_ID = 'esp32c3_01'
const MQTT_TEMP_TOPIC = 'sensor/c3/temp'
const MQTT_MODE_TOPIC = 'pump/c3/mode'
const MQTT_CMD_TOPIC = 'pump/c3/cmd'
const AUDIO_POLL_INTERVAL_MS = 5000

const API_BASE =
  (import.meta.env.VITE_API_URL as string | undefined)?.replace(/\/$/, '') ||
  'http://localhost:3000'
const MQTT_WS_URL = `${API_BASE.replace(/^http/, 'ws')}/ws/mqtt`

function viewFromHash(hash: string): AppView {
  return hash === WATER_PUMP_HASH ? 'water-pump' : 'pager'
}

function getInitialView(): AppView {
  if (typeof window === 'undefined') {
    return 'pager'
  }

  return viewFromHash(window.location.hash)
}

function formatDateTime(isoDate: string): string {
  const parsed = new Date(isoDate)
  if (Number.isNaN(parsed.getTime())) {
    return isoDate
  }

  return parsed.toLocaleString()
}

function sortAudioRecordsOldestFirst(records: AudioRecord[]): AudioRecord[] {
  return [...records].sort((left, right) => {
    const leftTime = new Date(left.createdAt).getTime()
    const rightTime = new Date(right.createdAt).getTime()

    if (Number.isNaN(leftTime) || Number.isNaN(rightTime)) {
      return left.id.localeCompare(right.id)
    }

    if (leftTime === rightTime) {
      return left.id.localeCompare(right.id)
    }

    return leftTime - rightTime
  })
}

function formatTemp(payload: string): string {
  const value = Number(payload)
  if (!Number.isFinite(value)) {
    return payload
  }

  return `${value.toFixed(2)} C`
}

function normalizePumpState(payload: string): string {
  const text = payload.trim().toUpperCase()

  if (text.includes('ON')) {
    return 'ON'
  }

  if (text.includes('OFF')) {
    return 'OFF'
  }

  if (text.includes('AUTO')) {
    return 'AUTO'
  }

  if (text.includes('MANUAL')) {
    return 'MANUAL'
  }

  return payload
}

function parseJsonMessage(raw: string): BridgeMessage | null {
  try {
    const parsed = JSON.parse(raw) as BridgeMessage
    return parsed
  } catch {
    return null
  }
}

function isPumpOn(state: string): boolean {
  return state.trim().toUpperCase() === 'ON'
}

function statusWithTimestamp(message: string): string {
  return `[${new Date().toLocaleTimeString()}] ${message}`
}

async function readErrorMessage(response: Response): Promise<string> {
  try {
    const payload = (await response.json()) as { error?: string; details?: string }
    if (payload.error) {
      return payload.error
    }
    if (payload.details) {
      return payload.details
    }
  } catch {
    // Fall back to status text.
  }

  return response.statusText || `Request failed (${response.status})`
}

type PagerViewProps = {
  records: AudioRecord[]
  isLoading: boolean
  deletingRecordId: string | null
  statusLines: string[]
  cardsEndRef: RefObject<HTMLDivElement | null>
  onDeleteRecord: (recordId: string) => Promise<void>
}

type FetchAudioOptions = {
  showLoading?: boolean
  logSuccess?: boolean
  logError?: boolean
}

function PagerView({
  records,
  isLoading,
  deletingRecordId,
  statusLines,
  cardsEndRef,
  onDeleteRecord,
}: PagerViewProps) {
  const statusText = statusLines.length > 0 ? statusLines.join('\n') : DEFAULT_STATUS_LINE

  return (
    <main className="content" data-node-id="86:8">
      <section className="leftColumn" aria-label="Pager recordings" data-node-id="96:42">
        <div className="cards" data-node-id="86:4140">
          {isLoading && records.length === 0 ? (
            <article className="card" aria-live="polite">
              <h2 className="cardTitle">Loading recordings...</h2>
              <div className="cardBody">
                <div className="cardContent">
                  <p className="cardMeta">Fetching /api/audio</p>
                  <p className="emptyCardText">Please wait...</p>
                </div>
              </div>
            </article>
          ) : null}

          {!isLoading && records.length === 0 ? (
            <article className="card" aria-live="polite">
              <h2 className="cardTitle">No recordings yet</h2>
              <div className="cardBody">
                <div className="cardContent">
                  <p className="cardMeta">No audio rows were returned from /api/audio.</p>
                  <p className="emptyCardText">Start recording from your M5 device to populate this page.</p>
                </div>
              </div>
            </article>
          ) : null}

          {records.map((record, index) => (
            <article className="card" key={record.id}>
              <h2 className="cardTitle">{record.deviceName}</h2>
              <div className={`cardBody ${index % 2 === 1 ? 'isWarning' : ''}`}>
                <div className="cardContent">
                  <p className="cardMeta">{formatDateTime(record.createdAt)}</p>
                  <audio
                    className="audioTrack audioPlayer"
                    controls
                    preload="none"
                    src={`${API_BASE}/api/audio/${record.id}/file`}
                  />
                  <div className="cardActions">
                    <button
                      type="button"
                      className="deleteButton"
                      onClick={() => {
                        void onDeleteRecord(record.id)
                      }}
                      disabled={deletingRecordId === record.id}
                    >
                      {deletingRecordId === record.id ? 'Deleting...' : 'Delete'}
                    </button>
                  </div>
                </div>
              </div>
            </article>
          ))}

          <div ref={cardsEndRef} aria-hidden="true" />
        </div>
      </section>

      <aside className="rightColumn" aria-label="Conversation status" data-node-id="95:8">
        <section className="statusCard" data-node-id="96:15">
          <p className="statusText">{statusText}</p>
        </section>
      </aside>
    </main>
  )
}

type WaterPumpViewProps = {
  snapshot: PumpSnapshot
  isSwitching: boolean
  statusLines: string[]
  onSwitch: () => Promise<void>
}

function WaterPumpView({ snapshot, isSwitching, statusLines, onSwitch }: WaterPumpViewProps) {
  const statusText = statusLines.length > 0 ? statusLines.join('\n') : DEFAULT_STATUS_LINE

  return (
    <main className="pumpContent" data-node-id="96:21">
      <section className="pumpTop" data-node-id="96:22" aria-label="Water pump device">
        <article className="pumpCard" data-node-id="96:23">
          <h2 className="cardTitle" data-node-id="96:24">
            {snapshot.deviceName}
          </h2>
          <div className="pumpBody" data-node-id="96:25">
            <div className="pumpBodyRow" data-node-id="96:26">
              <div className="pumpReadings" data-node-id="99:9">
                <div className="pumpReading" data-node-id="99:12">
                  <span>Pump State:</span>
                  <span className="pumpValue">{snapshot.state}</span>
                </div>
                <div className="pumpReading" data-node-id="99:14">
                  <span>Temperature:</span>
                  <span className="pumpValue">{snapshot.temp}</span>
                </div>
              </div>

              <button
                type="button"
                className="pumpSwitchButton"
                data-node-id="99:373"
                onClick={() => {
                  void onSwitch()
                }}
                disabled={isSwitching}
              >
                {isSwitching ? 'Sending...' : 'Switch'}
              </button>
            </div>
          </div>
        </article>
      </section>

      <section className="pumpStatusCard" data-node-id="96:39" aria-label="Conversation status">
        <div className="pumpStatusInner" data-node-id="96:40">
          <p className="statusText" data-node-id="96:41">
            {statusText}
          </p>
        </div>
      </section>
    </main>
  )
}

function App() {
  const [activeView, setActiveView] = useState<AppView>(getInitialView)
  const [audioRecords, setAudioRecords] = useState<AudioRecord[]>([])
  const [isAudioLoading, setIsAudioLoading] = useState(false)
  const [deletingRecordId, setDeletingRecordId] = useState<string | null>(null)
  const [isSwitching, setIsSwitching] = useState(false)
  const [pumpSnapshot, setPumpSnapshot] = useState<PumpSnapshot>({
    deviceName: PUMP_DEVICE_ID,
    state: 'OFF',
    temp: '--',
  })
  const [statusLines, setStatusLines] = useState<string[]>([])
  const audioFetchInFlightRef = useRef(false)
  const cardsEndRef = useRef<HTMLDivElement | null>(null)
  const previousAudioCountRef = useRef<number | null>(null)

  const addStatusLine = useCallback((message: string) => {
    setStatusLines((previous) => {
      const next = [...previous, statusWithTimestamp(message)]
      return next.slice(-MAX_STATUS_LINES)
    })
  }, [])

  const fetchAudioRecords = useCallback(async (options?: FetchAudioOptions) => {
    if (audioFetchInFlightRef.current) {
      return
    }

    audioFetchInFlightRef.current = true

    const showLoading = options?.showLoading ?? true
    const logSuccess = options?.logSuccess ?? true
    const logError = options?.logError ?? true

    if (showLoading) {
      setIsAudioLoading(true)
    }

    try {
      const response = await fetch(`${API_BASE}/api/audio`)
      if (!response.ok) {
        throw new Error(await readErrorMessage(response))
      }

      const payload = (await response.json()) as Array<{ id: string; deviceId: string; createdAt: string }>
      const mapped: AudioRecord[] = payload.map((record) => ({
        id: record.id,
        deviceName: record.deviceId,
        createdAt: record.createdAt,
      }))
      const incoming = sortAudioRecordsOldestFirst(mapped)

      setAudioRecords((previous) => {
        const previousById = new Map(previous.map((record) => [record.id, record]))
        const incomingById = new Map(incoming.map((record) => [record.id, record]))

        const retained = previous
          .filter((record) => incomingById.has(record.id))
          .map((record) => incomingById.get(record.id) ?? record)

        const appended = incoming.filter((record) => !previousById.has(record.id))
        return [...retained, ...appended]
      })

      if (logSuccess) {
        addStatusLine(`Loaded ${incoming.length} audio record(s).`)
      }
    } catch (error) {
      const message = error instanceof Error ? error.message : 'Unknown error'
      if (logError) {
        addStatusLine(`Failed to load audio list: ${message}`)
      }
    } finally {
      if (showLoading) {
        setIsAudioLoading(false)
      }

      audioFetchInFlightRef.current = false
    }
  }, [addStatusLine])

  const deleteAudioRecord = useCallback(
    async (recordId: string) => {
      setDeletingRecordId(recordId)

      try {
        const response = await fetch(`${API_BASE}/api/audio/${recordId}`, {
          method: 'DELETE',
        })

        if (!response.ok) {
          throw new Error(await readErrorMessage(response))
        }

        setAudioRecords((previous) => previous.filter((record) => record.id !== recordId))
        addStatusLine(`Deleted audio record ${recordId}.`)
      } catch (error) {
        const message = error instanceof Error ? error.message : 'Unknown error'
        addStatusLine(`Failed to delete ${recordId}: ${message}`)
      } finally {
        setDeletingRecordId(null)
      }
    },
    [addStatusLine],
  )

  const publishMqttMessage = useCallback(async (topic: string, message: string) => {
    const response = await fetch(`${API_BASE}/api/mqtt/publish`, {
      method: 'POST',
      headers: {
        'Content-Type': 'application/json',
      },
      body: JSON.stringify({ topic, message }),
    })

    if (!response.ok) {
      throw new Error(await readErrorMessage(response))
    }
  }, [])

  const switchPump = useCallback(async () => {
    if (isSwitching) {
      return
    }

    setIsSwitching(true)
    const nextState = isPumpOn(pumpSnapshot.state) ? 'OFF' : 'ON'

    try {
      await publishMqttMessage(MQTT_MODE_TOPIC, 'MANUAL')
      await publishMqttMessage(MQTT_CMD_TOPIC, nextState)
      setPumpSnapshot((previous) => ({
        ...previous,
        state: nextState,
      }))
      addStatusLine(`Pump command sent: ${nextState}`)
    } catch (error) {
      const message = error instanceof Error ? error.message : 'Unknown error'
      addStatusLine(`Failed to switch pump: ${message}`)
    } finally {
      setIsSwitching(false)
    }
  }, [addStatusLine, isSwitching, publishMqttMessage, pumpSnapshot.state])

  useEffect(() => {
    if (typeof window === 'undefined') {
      return
    }

    const syncFromHash = () => {
      setActiveView(viewFromHash(window.location.hash))
    }

    if (!window.location.hash) {
      const nextUrl = `${window.location.pathname}${window.location.search}${PAGER_HASH}`
      window.history.replaceState(null, '', nextUrl)
    }

    syncFromHash()
    window.addEventListener('hashchange', syncFromHash)

    return () => {
      window.removeEventListener('hashchange', syncFromHash)
    }
  }, [])

  useEffect(() => {
    void fetchAudioRecords()
  }, [fetchAudioRecords])

  useEffect(() => {
    if (activeView !== 'pager') {
      return
    }

    void fetchAudioRecords({
      showLoading: false,
      logSuccess: false,
      logError: false,
    })

    const intervalId = window.setInterval(() => {
      void fetchAudioRecords({
        showLoading: false,
        logSuccess: false,
        logError: false,
      })
    }, AUDIO_POLL_INTERVAL_MS)

    return () => {
      window.clearInterval(intervalId)
    }
  }, [activeView, fetchAudioRecords])

  useEffect(() => {
    if (activeView !== 'pager') {
      return
    }

    const previousCount = previousAudioCountRef.current
    const currentCount = audioRecords.length
    const hasAppendedRecords = previousCount !== null && currentCount > previousCount

    previousAudioCountRef.current = currentCount

    if (hasAppendedRecords) {
      cardsEndRef.current?.scrollIntoView({ behavior: 'smooth', block: 'end' })
    }
  }, [activeView, audioRecords.length])

  useEffect(() => {
    const socket = new WebSocket(MQTT_WS_URL)

    socket.addEventListener('open', () => {
      addStatusLine('MQTT bridge connected.')

      const subscribeToTopics = [MQTT_TEMP_TOPIC, 'pump/status', 'pump/c3/state']
      subscribeToTopics.forEach((topic) => {
        socket.send(JSON.stringify({ action: 'subscribe', topic }))
      })
    })

    socket.addEventListener('message', (event) => {
      const raw = typeof event.data === 'string' ? event.data : ''
      const message = parseJsonMessage(raw)

      if (!message) {
        return
      }

      if (message.subscribed) {
        addStatusLine(`Subscribed to ${message.subscribed}.`)
        return
      }

      if (message.error) {
        addStatusLine(`MQTT error: ${message.error}`)
        return
      }

      const payload = message.payload

      if (!message.topic || payload === undefined) {
        return
      }

      addStatusLine(`${message.topic}: ${payload}`)

      if (message.topic === MQTT_TEMP_TOPIC) {
        setPumpSnapshot((previous) => ({
          ...previous,
          temp: formatTemp(payload),
        }))
      } else if (message.topic === 'pump/status' || message.topic === 'pump/c3/state') {
        setPumpSnapshot((previous) => ({
          ...previous,
          state: normalizePumpState(payload),
        }))
      }
    })

    socket.addEventListener('close', () => {
      addStatusLine('MQTT bridge disconnected.')
    })

    socket.addEventListener('error', () => {
      addStatusLine('MQTT bridge connection error.')
    })

    return () => {
      socket.close()
    }
  }, [addStatusLine])

  const currentPumpSnapshot = useMemo(
    () => ({
      ...pumpSnapshot,
      temp: pumpSnapshot.temp === '--' ? '-- C' : pumpSnapshot.temp,
    }),
    [pumpSnapshot],
  )

  const navNodeId = activeView === 'pager' ? '75:28' : '75:32'

  return (
    <div className="page" data-node-id={activeView === 'pager' ? '1:3' : '75:3'}>
      <header className="navbar" data-node-id={navNodeId}>
        <nav className="navContent" aria-label="Primary">
          <a href={PAGER_HASH} className={`navItem ${activeView === 'pager' ? 'isActive' : ''}`}>
            Pager
          </a>
          <a
            href={WATER_PUMP_HASH}
            className={`navItem ${activeView === 'water-pump' ? 'isActive' : ''}`}
          >
            Water Pump
          </a>
        </nav>
      </header>

      {activeView === 'pager' ? (
        <PagerView
          records={audioRecords}
          isLoading={isAudioLoading}
          deletingRecordId={deletingRecordId}
          statusLines={statusLines}
          cardsEndRef={cardsEndRef}
          onDeleteRecord={deleteAudioRecord}
        />
      ) : (
        <WaterPumpView
          snapshot={currentPumpSnapshot}
          isSwitching={isSwitching}
          statusLines={statusLines}
          onSwitch={switchPump}
        />
      )}
    </div>
  )
}

export default App
