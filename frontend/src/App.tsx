import './App.css'

type PagerCard = {
  id: number
  deviceName: string
  dateTime: string
  tone: 'minimal' | 'warning'
}

const pagerCards: PagerCard[] = [
  {
    id: 1,
    deviceName: '{deviceName}',
    dateTime: '{Date: Time}',
    tone: 'minimal',
  },
  {
    id: 2,
    deviceName: '{deviceName}',
    dateTime: '{Date: Time}',
    tone: 'warning',
  },
]

function App() {
  return (
    <div className="page" data-node-id="1:3">
      <header className="navbar" data-node-id="75:28">
        <nav className="navContent" aria-label="Primary">
          <a href="#" className="navItem isActive">
            Pager
          </a>
          <span className="navItem">Water Pump</span>
        </nav>
      </header>

      <main className="content" data-node-id="86:8">
        <section className="cards" aria-label="Pager recordings">
          {pagerCards.map((card) => (
            <article className="card" key={card.id}>
              <h2 className="cardTitle">{card.deviceName}</h2>
              <div
                className={`cardBody ${card.tone === 'warning' ? 'isWarning' : ''}`}
              >
                <p className="cardMeta">{card.dateTime}</p>
                <div className="audioTrack" aria-hidden="true" />
                <div className="cardActions">
                  <button type="button" className="deleteButton">
                    Delete
                  </button>
                </div>
              </div>
            </article>
          ))}
        </section>
      </main>
    </div>
  )
}

export default App
