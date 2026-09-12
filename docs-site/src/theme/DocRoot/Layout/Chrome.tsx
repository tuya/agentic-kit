import React, {useState} from 'react';
import Link from '@docusaurus/Link';
import {useLocation} from '@docusaurus/router';
import {useDocsSidebar} from '@docusaurus/plugin-content-docs/client';
import type {PropSidebarItem} from '@docusaurus/plugin-content-docs';

// Topbar + sidebar, rendered in DocRoot/Layout — above the per-page route, so
// a doc-page navigation only swaps the article: these components never
// remount, and the sidebar keeps its scroll position and <details> state.
// categoryOpenState only seeds the initial open state (category containing
// the current page) before the user has toggled it.
const categoryOpenState = new Map<string, boolean>();

function categoryContains(item: PropSidebarItem, href: string): boolean {
  if (item.type === 'link') return item.href === href;
  if (item.type === 'category') return item.items.some(child => categoryContains(child, href));
  return false;
}

function SidebarItem({item, current}: {item: PropSidebarItem; current: string}) {
  if (item.type === 'link') return <Link className={item.href === current ? 'active' : undefined} to={item.href}>{item.label}</Link>;
  if (item.type === 'category') {
    const open = categoryOpenState.get(item.label) ?? categoryContains(item, current);
    return <details open={open} onToggle={event => categoryOpenState.set(item.label, event.currentTarget.open)}><summary>{item.label}</summary>{item.items.map((child, index) => <SidebarItem key={index} item={child} current={current} />)}</details>;
  }
  return null;
}

export function Topbar() {
  const [menuOpen, setMenuOpen] = useState(false);
  return <>
    <a className="skip" href="#content">跳到正文</a>
    <header className="topbar"><div className="topbar-inner">
      <div className="brand"><a className="tuya-logo-link" href="https://www.tuya.com/" target="_blank" rel="noopener noreferrer" aria-label="访问涂鸦官网"><img className="tuya-logo" src="/img/tuya-logo.png" alt="Tuya" width="51" height="27" /></a><Link className="brand-name-link" to="/" aria-label="返回 Tuya Agentic-kit 首页"><span className="brand-rule" /><span className="brand-name">Agentic-kit</span><span className="docs-badge">DOCS</span></Link></div>
      <button className="menu" type="button" aria-controls="topnav" aria-expanded={menuOpen} onClick={() => setMenuOpen(!menuOpen)}>菜单</button>
      <nav className={`topnav${menuOpen ? ' open' : ''}`} id="topnav" aria-label="顶部导航" onClick={event => {if ((event.target as HTMLElement).closest('a')) setMenuOpen(false);}} onKeyDown={event => {if (event.key === 'Escape') setMenuOpen(false);}}><a href="https://developer.tuya.com/cn/" target="_blank" rel="noopener noreferrer">涂鸦开发者 ↗</a><a href="https://github.com/tuya/agentic-kit" target="_blank" rel="noopener noreferrer">GitHub ↗</a><Link className="primary" to="/docs/tutorials/quick-start">快速开始</Link></nav>
    </div></header>
  </>;
}

export function Sidebar() {
  const sidebar = useDocsSidebar();
  const current = useLocation().pathname.replace(/\/$/, '');
  const navLink = (href: string, label: string) => <Link className={current === href ? 'active' : undefined} to={href}>{label}</Link>;
  return (
    <aside className="sidebar" aria-label="文档导航">
      <div className="side-group page-tabs">{navLink('/docs/intro', '介绍')}{navLink('/docs/concepts', '核心概念')}{navLink('/docs/architecture', '架构')}</div>
      {sidebar?.items.filter(item => item.type === 'category').map((item, index) => (
        <div className="side-group" key={index}>
          <div className="side-label">{item.label}</div>
          {item.items.map((child, childIndex) => <SidebarItem key={childIndex} item={child} current={current} />)}
        </div>
      ))}
      <div className="side-group">{sidebar?.items.filter(item => item.type === 'link' && !['/docs/intro', '/docs/concepts', '/docs/architecture'].includes(item.href)).map((item, index) => <SidebarItem key={index} item={item} current={current} />)}</div>
    </aside>
  );
}
