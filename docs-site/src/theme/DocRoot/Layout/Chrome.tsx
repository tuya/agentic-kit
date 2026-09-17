import React, {useState} from 'react';
import Link from '@docusaurus/Link';
import Translate, {translate} from '@docusaurus/Translate';
import {useLocation} from '@docusaurus/router';
import {useDocsSidebar} from '@docusaurus/plugin-content-docs/client';
import {useAlternatePageUtils} from '@docusaurus/theme-common/internal';
import useDocusaurusContext from '@docusaurus/useDocusaurusContext';
import type {PropSidebarItem} from '@docusaurus/plugin-content-docs';

// Topbar + sidebar, rendered in DocRoot/Layout — above the per-page route, so
// a doc-page navigation only swaps the article: these components never
// remount, and the sidebar keeps its scroll position and <details> state.
// categoryOpenState only seeds the initial open state (category containing
// the current page) before the user has toggled it.
const categoryOpenState = new Map<string, boolean>();
const pageTabPaths = new Set(['/docs/intro', '/docs/concepts', '/docs/architecture']);

function normalizePath(pathname: string, localeBaseUrl: string): string {
  const withoutBaseUrl = pathname.startsWith(localeBaseUrl)
    ? pathname.slice(localeBaseUrl.length)
    : pathname.replace(/^\//, '');
  return `/${withoutBaseUrl}`.replace(/\/$/, '') || '/';
}

function categoryContains(item: PropSidebarItem, current: string, baseUrl: string): boolean {
  if (item.type === 'link') return normalizePath(item.href, baseUrl) === current;
  if (item.type === 'category') return item.items.some(child => categoryContains(child, current, baseUrl));
  return false;
}

function SidebarItem({item, current, baseUrl}: {item: PropSidebarItem; current: string; baseUrl: string}) {
  if (item.type === 'link') return <Link className={normalizePath(item.href, baseUrl) === current ? 'active' : undefined} to={item.href}>{item.label}</Link>;
  if (item.type === 'category') {
    const open = categoryOpenState.get(item.label) ?? categoryContains(item, current, baseUrl);
    return <details open={open} onToggle={event => categoryOpenState.set(item.label, event.currentTarget.open)}><summary>{item.label}</summary>{item.items.map((child, index) => <SidebarItem key={index} item={child} current={current} baseUrl={baseUrl} />)}</details>;
  }
  return null;
}

function LocaleSelector() {
  const {
    i18n: {currentLocale, locales, localeConfigs},
  } = useDocusaurusContext();
  const {createUrl} = useAlternatePageUtils();
  const {search, hash} = useLocation();

  const changeLocale = (event: React.ChangeEvent<HTMLSelectElement>) => {
    const pathname = createUrl({locale: event.target.value, fullyQualified: false});
    window.location.assign(`${pathname}${search}${hash}`);
  };

  return <label className="locale-selector">
    <span className="sr-only"><Translate id="docs.topbar.languageSelector.label" description="文档顶部语言选择器标签">选择语言</Translate></span>
    <select aria-label={translate({id: 'docs.topbar.languageSelector.ariaLabel', message: '选择网站语言', description: '文档顶部语言选择器的无障碍标签'})} value={currentLocale} onChange={changeLocale}>
      {locales.map(locale => <option key={locale} value={locale} lang={localeConfigs[locale].htmlLang}>{localeConfigs[locale].label}</option>)}
    </select>
  </label>;
}

export function Topbar() {
  const [menuOpen, setMenuOpen] = useState(false);
  const {i18n: {currentLocale}} = useDocusaurusContext();
  const developerUrl = currentLocale === 'en' ? 'https://developer.tuya.com/en/' : 'https://developer.tuya.com/cn/';
  return <>
    <a className="skip" href="#content"><Translate id="docs.topbar.skipToContent" description="跳过导航并进入正文的链接">跳到正文</Translate></a>
    <header className="topbar"><div className="topbar-inner">
      <div className="brand"><a className="tuya-logo-link" href="https://www.tuya.com/" target="_blank" rel="noopener noreferrer" aria-label={translate({id: 'docs.topbar.tuyaHome.ariaLabel', message: '访问涂鸦官网', description: '涂鸦徽标链接的无障碍标签'})}><img className="tuya-logo" src="/img/tuya-logo.png" alt={translate({id: 'docs.topbar.tuyaLogo.alt', message: '涂鸦', description: '涂鸦徽标的替代文本'})} width="51" height="27" /></a><Link className="brand-name-link" to="/" aria-label={translate({id: 'docs.topbar.docsHome.ariaLabel', message: '返回 Tuya Agentic-kit 首页', description: '文档品牌链接的无障碍标签'})}><span className="brand-rule" /><span className="brand-name">Agentic-kit</span><span className="docs-badge"><Translate id="docs.topbar.docsBadge" description="文档品牌徽章">文档</Translate></span></Link></div>
      <button className="menu" type="button" aria-controls="topnav" aria-expanded={menuOpen} aria-label={translate({id: 'docs.topbar.menu.ariaLabel', message: '切换顶部导航菜单', description: '移动端菜单按钮的无障碍标签'})} onClick={() => setMenuOpen(!menuOpen)}><Translate id="docs.topbar.menu.label" description="移动端菜单按钮文字">菜单</Translate></button>
      <nav className={`topnav${menuOpen ? ' open' : ''}`} id="topnav" aria-label={translate({id: 'docs.topbar.navigation.ariaLabel', message: '顶部导航', description: '文档顶部导航的无障碍标签'})} onClick={event => {if ((event.target as HTMLElement).closest('a')) setMenuOpen(false);}} onKeyDown={event => {if (event.key === 'Escape') setMenuOpen(false);}}><a href={developerUrl} target="_blank" rel="noopener noreferrer"><Translate id="docs.topbar.tuyaDeveloper" description="涂鸦开发者网站链接">涂鸦开发者 ↗</Translate></a><a href="https://github.com/tuya/agentic-kit" target="_blank" rel="noopener noreferrer"><Translate id="docs.topbar.github" description="GitHub 仓库链接">GitHub ↗</Translate></a><LocaleSelector /><Link className="primary" to="/docs/tutorials/quick-start"><Translate id="docs.topbar.quickStart" description="文档顶部快速开始按钮">快速开始</Translate></Link></nav>
    </div></header>
  </>;
}

export function Sidebar() {
  const sidebar = useDocsSidebar();
  const {i18n: {currentLocale, localeConfigs}} = useDocusaurusContext();
  const localeBaseUrl = localeConfigs[currentLocale].baseUrl;
  const current = normalizePath(useLocation().pathname, localeBaseUrl);
  const topLinks = sidebar?.items.filter((item): item is Extract<PropSidebarItem, {type: 'link'}> => item.type === 'link' && pageTabPaths.has(normalizePath(item.href, localeBaseUrl))) ?? [];
  return (
    <aside className="sidebar" aria-label={translate({id: 'docs.sidebar.navigation.ariaLabel', message: '文档导航', description: '文档侧边栏的无障碍标签'})}>
      <div className="side-group page-tabs">{topLinks.map((item, index) => <SidebarItem key={index} item={item} current={current} baseUrl={localeBaseUrl} />)}</div>
      {sidebar?.items.filter(item => item.type === 'category').map((item, index) => (
        <div className="side-group" key={index}>
          <div className="side-label">{item.label}</div>
          {item.items.map((child, childIndex) => <SidebarItem key={childIndex} item={child} current={current} baseUrl={localeBaseUrl} />)}
        </div>
      ))}
      <div className="side-group">{sidebar?.items.filter(item => item.type === 'link' && !pageTabPaths.has(normalizePath(item.href, localeBaseUrl))).map((item, index) => <SidebarItem key={index} item={item} current={current} baseUrl={localeBaseUrl} />)}</div>
    </aside>
  );
}
