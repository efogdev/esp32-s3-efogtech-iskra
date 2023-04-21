import { h, Component } from 'preact';
import { Link } from 'preact-router';
import style from './style.less';
import cn from 'classnames';

class Icon extends Component {
	render(props) {
		return (
			<svg className={cn(style.logo, { [style.heating]: props.heating })} viewBox="0 0 448 512" xmlns="http://www.w3.org/2000/svg"><path d="M302.5 512c23.18 0 44.43-12.58 56-32.66C374.69 451.26 384 418.75 384 384c0-36.12-10.08-69.81-27.44-98.62L400 241.94l9.38 9.38c6.25 6.25 16.38 6.25 22.63 0l11.3-11.32c6.25-6.25 6.25-16.38 0-22.63l-52.69-52.69c-6.25-6.25-16.38-6.25-22.63 0l-11.31 11.31c-6.25 6.25-6.25 16.38 0 22.63l9.38 9.38-39.41 39.41c-11.56-11.37-24.53-21.33-38.65-29.51V63.74l15.97-.02c8.82-.01 15.97-7.16 15.98-15.98l.04-31.72C320 7.17 312.82-.01 303.97 0L80.03.26c-8.82.01-15.97 7.16-15.98 15.98l-.04 31.73c-.01 8.85 7.17 16.02 16.02 16.01L96 63.96v153.93C38.67 251.1 0 312.97 0 384c0 34.75 9.31 67.27 25.5 95.34C37.08 499.42 58.33 512 81.5 512h221zM120.06 259.43L144 245.56V63.91l96-.11v181.76l23.94 13.87c24.81 14.37 44.12 35.73 56.56 60.57h-257c12.45-24.84 31.75-46.2 56.56-60.57z"/></svg>
		)
	}
}

const WifiGoodIcon = () => (
	<img className={style.good} src="data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAADAAAAAwCAQAAAD9CzEMAAACLUlEQVR42u2XzUtUURTAf/o0xhJEyixdSRAorRQKZAKLmCb8E3QRCGkgLkJKBlQKEaGpoZwQF0VOVu4FcVGzElezaNHCD/wiFRcytvB7zNfC5/O+mfvevPkCF++c1cw953fuuefcM3fAEUccOQdykXv4GGGaFaLEiBFlhWlG8NFAUSboK7QS5hDVQg/4yRMupw6v42sStKiHfKHWPvwWk7bRok5QkxxeTJBYWngVlRjvuWSFv8Nc2vBTXcRthu/gyNJ1kxkiRJhlM0keTxPhCkET8yN+0IWbUoN9KXfxETbdUgDFGOC71GyJZ1y1rFk5nSxLfUNGw0b24wzWeBy/C6BEUsQCWliP897DG2/2iD19+ZhBAZTHbfoY5zfb2uofwgzg5YLQfUMc6/67eGTperUQUR7q31USYM20nFGC3BRO4a+Gf2B2oh52mdNdygkIWZnpPz5RoXlUs8AO962K5taL2sSW7d7folnzuka9nWGh8C7lCzZMof0BPZHWHZ60HhPi/kelgH3mmWKK+YSWPtExCuzmoBAyuO7wAQ8ufd2Fh6DWtKf6zT4eIJ/Pepe8oUxqU4ZfHxWjkkuZNMRHVNatm44GVlEJpY4/CdFNpfCpljZ66KWdeuE4rtNDfqY//AqtLBpOfIPnwqDIUKqISLvmFzeyE+ClaecPZCsHvxQ/SF72nl99CfihbOIBXhnwb3PxiDyrhT9X79QXqKi8zuVTuJt+5/+AI46cO/kPf/cSLAl9DOYAAAAASUVORK5CYII=" alt="" />
)

const WifiBadIcon = () => (
	<svg className={style.bad} viewBox="0 0 24 24" xmlns="http://www.w3.org/2000/svg"><g><path d="M0 0h24v24H0z" fill="none"/><path d="M12 18c.714 0 1.37.25 1.886.666L12 21l-1.886-2.334A2.987 2.987 0 0 1 12 18zM2.808 1.393l17.677 17.678-1.414 1.414-5.18-5.18A5.994 5.994 0 0 0 12 15c-1.428 0-2.74.499-3.77 1.332l-1.256-1.556a7.963 7.963 0 0 1 4.622-1.766L9 10.414a10.969 10.969 0 0 0-3.912 2.029L3.83 10.887A12.984 12.984 0 0 1 7.416 8.83L5.132 6.545a16.009 16.009 0 0 0-3.185 2.007L.689 6.997c.915-.74 1.903-1.391 2.952-1.942L1.393 2.808l1.415-1.415zM14.5 10.285l-2.284-2.283L12 8c3.095 0 5.937 1.081 8.17 2.887l-1.258 1.556a10.96 10.96 0 0 0-4.412-2.158zM12 3c4.285 0 8.22 1.497 11.31 3.997l-1.257 1.555A15.933 15.933 0 0 0 12 5c-.878 0-1.74.07-2.58.207L7.725 3.51C9.094 3.177 10.527 3 12 3z"/></g></svg>
)

const LoadingIcon = () => (
	<svg className={style.loading} viewBox="0 0 100 100">
		<circle cx="50" cy="50" fill="none" stroke="#ffffff" stroke-width="14" r="35" stroke-dasharray="164.93361431346415 56.97787143782138">
			<animateTransform attributeName="transform" type="rotate" repeatCount="indefinite" dur="1.6949152542372883s" values="0 50 50;360 50 50" keyTimes="0;1"></animateTransform>
		</circle>
	</svg>
)

const CoolingIcon = () => (
	<svg className={style.cooling} viewBox="0 0 512 512" width="512" xmlns="http://www.w3.org/2000/svg"><title/><path d="M461,349l-34-19.64a89.53,89.53,0,0,1,20.94-16,22,22,0,0,0-21.28-38.51,133.62,133.62,0,0,0-38.55,32.1L300,256l88.09-50.86a133.46,133.46,0,0,0,38.55,32.1,22,22,0,1,0,21.28-38.51,89.74,89.74,0,0,1-20.94-16l34-19.64A22,22,0,1,0,439,125l-34,19.63a89.74,89.74,0,0,1-3.42-26.15A22,22,0,0,0,380,96h-.41a22,22,0,0,0-22,21.59A133.61,133.61,0,0,0,366.09,167L278,217.89V116.18a133.5,133.5,0,0,0,47.07-17.33,22,22,0,0,0-22.71-37.69A89.56,89.56,0,0,1,278,71.27V38a22,22,0,0,0-44,0V71.27a89.56,89.56,0,0,1-24.36-10.11,22,22,0,1,0-22.71,37.69A133.5,133.5,0,0,0,234,116.18V217.89L145.91,167a133.61,133.61,0,0,0,8.52-49.43,22,22,0,0,0-22-21.59H132a22,22,0,0,0-21.59,22.41A89.74,89.74,0,0,1,107,144.58L73,125a22,22,0,1,0-22,38.1l34,19.64a89.74,89.74,0,0,1-20.94,16,22,22,0,1,0,21.28,38.51,133.62,133.62,0,0,0,38.55-32.1L212,256l-88.09,50.86a133.62,133.62,0,0,0-38.55-32.1,22,22,0,1,0-21.28,38.51,89.74,89.74,0,0,1,20.94,16L51,349a22,22,0,1,0,22,38.1l34-19.63a89.74,89.74,0,0,1,3.42,26.15A22,22,0,0,0,132,416h.41a22,22,0,0,0,22-21.59A133.61,133.61,0,0,0,145.91,345L234,294.11V395.82a133.5,133.5,0,0,0-47.07,17.33,22,22,0,1,0,22.71,37.69A89.56,89.56,0,0,1,234,440.73V474a22,22,0,0,0,44,0V440.73a89.56,89.56,0,0,1,24.36,10.11,22,22,0,0,0,22.71-37.69A133.5,133.5,0,0,0,278,395.82V294.11L366.09,345a133.61,133.61,0,0,0-8.52,49.43,22,22,0,0,0,22,21.59H380a22,22,0,0,0,21.59-22.41A89.74,89.74,0,0,1,405,367.42l34,19.63A22,22,0,1,0,461,349Z"/></svg>
)

const PowerSupplyIcon = () => (
	<svg className={style.psu} viewBox="0 0 24 24" xmlns="http://www.w3.org/2000/svg"><title/><path d="M18.87,12.07l-6,9.47A1,1,0,0,1,12,22a.9.9,0,0,1-.28,0A1,1,0,0,1,11,21V15H6.82a2,2,0,0,1-1.69-3.07l6-9.47A1,1,0,0,1,12.28,2,1,1,0,0,1,13,3V9h4.18a2,2,0,0,1,1.69,3.07Z" /></svg>
)

export default class Header extends Component {
	constructor(props) {
		super(props)

		this.state = {}
	}

	componentDidMount() {
		window.emitter.on('refresh', () => {
			setTimeout(() => this.forceUpdate())
		})
	}

	render(props) {
		const { isHeating, isVoltageOk, isOnline, isLoading, isCooling } = Object.assign({}, this.state, window.store)

		return (
			<header class={style.header}>
				<h1>
					{!isVoltageOk && <PowerSupplyIcon />}

					{isOnline && <WifiGoodIcon />}
					{!isOnline && <WifiBadIcon />}

					<Icon heating={isHeating} />

					{isCooling && <CoolingIcon />}
					{isLoading && <LoadingIcon />}
				</h1>
				<nav>
					<Link href="/">Home</Link>
					<Link href="/settings">Settings</Link>
				</nav>
			</header>
		)
	}
}
