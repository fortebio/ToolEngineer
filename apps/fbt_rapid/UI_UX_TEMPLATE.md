# UI/UX Template — "Homies" design system

Khuôn mẫu UI/UX cô đặc từ dự án Homies HRM để tái dùng cho dự án mới. Copy phần **PROMPT** vào chat AI khi khởi tạo dự án; giữ phần **SPEC** làm chuẩn thiết kế.

---

## PROMPT (dán khi bắt đầu dự án mới)

> Xây dựng UI dashboard theo design system sau. Stack: **Next.js (App Router) + React + TypeScript strict + Tailwind CSS v4** (`@theme` tokens trong `app/globals.css`) + primitives kiểu **shadcn/ui** (Radix + `class-variance-authority` + `cn()` = `clsx`+`tailwind-merge`) trong `components/ui/`. `@/` alias → repo root (không có `src/`).
>
> **Quy tắc bất di:**
> - **Chỉ dùng `@theme` token, KHÔNG hardcode hex.** Ngoại lệ hạn chế: màu literal của recharts, hằng `CARD_SHADOW`, chữ trắng trên nút màu.
> - Màu theo mô hình semantic: `background / foreground / card / primary / secondary / muted / accent / destructive / border / input / ring` + custom `success / warning / info / surface-elevated / surface-sunken`. Định nghĩa bằng **oklch** (trừ `--primary` navy giữ hex).
> - **Card recipe** cố định: `bg-card rounded-2xl p-4 sm:p-6 border border-border` + `style={{ boxShadow: CARD_SHADOW }}`.
> - Font: sans **DM Sans**, serif **Source Serif 4**, mono **JetBrains Mono**. Radius gốc `0.75rem`.
> - Dùng `type` thay `interface`; string-literal union thay `enum`.
> - Nạp data trong `useEffect`/store hook, **không** trong render; guard browser API bằng `typeof window !== 'undefined'`; luôn `try/catch` quanh `JSON.parse`.
> - Animation: dùng các util `animate-fade-in / animate-slide-up / animate-scale-in / card-hover` + `stagger-1..8` (đã có sẵn trong globals.css bên dưới).
> - Component có biến thể → dùng `cva` với `data-slot` + `focus-visible:ring-ring/50 ring-[3px]` như mẫu Button.
>
> Bắt đầu bằng cách tạo `app/globals.css` (token block), `lib/utils.ts` (`cn` + `CARD_SHADOW`), và `components/ui/button.tsx` theo SPEC. Sau đó dựng shell: một route gated theo auth (loader → login → app), sidebar + main-content chuyển `Section` union bằng state (không router).

---

## SPEC

### 1. Design tokens — `app/globals.css`

```css
@import 'tailwindcss';
@import 'tw-animate-css';
@custom-variant dark (&:is(.dark *));

:root {
  --background: oklch(0.965 0.003 250);
  --foreground: oklch(0.2 0.015 250);
  --card: oklch(1 0 0);
  --card-foreground: oklch(0.2 0.015 250);
  --popover: oklch(1 0 0);
  --popover-foreground: oklch(0.2 0.015 250);
  --primary: #0a1f47;                    /* navy sâu — dấu ấn thương hiệu */
  --primary-foreground: oklch(0.99 0 0);
  --secondary: oklch(0.97 0.003 250);
  --secondary-foreground: oklch(0.3 0.015 250);
  --muted: oklch(0.96 0.003 250);
  --muted-foreground: oklch(0.55 0.01 250);
  --accent: oklch(0.78 0.18 85);         /* amber */
  --accent-foreground: oklch(0.25 0.05 85);
  --destructive: oklch(0.6 0.2 25);
  --destructive-foreground: oklch(0.99 0 0);
  --border: oklch(0.92 0.005 250);
  --input: oklch(0.97 0.003 250);
  --ring: oklch(0.55 0.12 250);
  --chart-1: oklch(0.55 0.18 250);
  --chart-2: oklch(0.65 0.15 155);
  --chart-3: oklch(0.7 0.18 350);
  --chart-4: oklch(0.78 0.18 85);
  --chart-5: oklch(0.6 0.12 200);
  --radius: 0.75rem;
  /* sidebar-* mirror các token trên; custom: */
  --surface-elevated: oklch(1 0 0);
  --surface-sunken: oklch(0.975 0.003 250);
  --success: oklch(0.6 0.17 155);
  --warning: oklch(0.78 0.18 85);
  --info: oklch(0.55 0.15 250);
}

@theme inline {
  --font-sans: 'DM Sans', 'DM Sans Fallback', system-ui;
  --font-serif: 'Source Serif 4', 'Georgia', serif;
  --font-mono: 'JetBrains Mono', 'Menlo', monospace;
  /* map mọi --color-*: var(--*) ; radius-sm/md/lg/xl = calc quanh --radius */
  --radius-sm: calc(var(--radius) - 4px);
  --radius-md: calc(var(--radius) - 2px);
  --radius-lg: var(--radius);
  --radius-xl: calc(var(--radius) + 4px);
}

@layer base {
  * { @apply border-border outline-ring/50; }
  body { @apply bg-background text-foreground; }
}
```

Kèm: scrollbar mảnh 8px (`::-webkit-scrollbar` + `.scrollbar-hidden`), `:focus-visible { outline: 2px solid var(--primary); offset 2px }`, `* { scroll-behavior: smooth }`.

### 2. Animation utilities (globals.css)

- Keyframes: `fade-in`, `slide-up` (Y+12px), `slide-in-left` (X−12px), `scale-in` (0.96), `shimmer`.
- Class: `.animate-fade-in` (0.4s), `.animate-slide-up` / `.animate-slide-in-left` / `.animate-scale-in` (dùng easing `cubic-bezier(0.16, 1, 0.3, 1)`), `.animate-shimmer` (skeleton loading).
- `.stagger-1 … .stagger-8` = `animation-delay` 0.05s → 0.4s (danh sách vào lần lượt).
- `.card-hover:hover` = `translateY(-2px)` + shadow sâu hơn.

### 3. `lib/utils.ts`

```ts
import { clsx, type ClassValue } from 'clsx'
import { twMerge } from 'tailwind-merge'
export function cn(...inputs: ClassValue[]) { return twMerge(clsx(inputs)) }

export const CARD_SHADOW =
  "rgba(14, 63, 126, 0.04) 0px 0px 0px 1px, rgba(42, 51, 69, 0.04) 0px 1px 1px -0.5px, rgba(42, 51, 70, 0.04) 0px 3px 3px -1.5px, rgba(42, 51, 70, 0.04) 0px 6px 6px -3px, rgba(14, 63, 126, 0.04) 0px 12px 12px -6px, rgba(14, 63, 126, 0.04) 0px 24px 24px -12px"
```

### 4. Component pattern (mẫu Button)

`cva` cho variants, `data-slot`, `Slot` (asChild), focus-ring 3px:

```tsx
const buttonVariants = cva(
  "inline-flex items-center justify-center gap-2 rounded-md text-sm font-medium transition-all disabled:opacity-50 outline-none focus-visible:border-ring focus-visible:ring-ring/50 focus-visible:ring-[3px]",
  {
    variants: {
      variant: {
        default: 'bg-primary text-primary-foreground hover:bg-primary/90',
        destructive: 'bg-destructive text-white hover:bg-destructive/90',
        outline: 'border bg-background shadow-xs hover:bg-accent hover:text-accent-foreground',
        secondary: 'bg-secondary text-secondary-foreground hover:bg-secondary/80',
        ghost: 'hover:bg-accent hover:text-accent-foreground',
        link: 'text-primary underline-offset-4 hover:underline',
      },
      size: { default: 'h-9 px-4 py-2', sm: 'h-8 px-3', lg: 'h-10 px-6', icon: 'size-9' },
    },
    defaultVariants: { variant: 'default', size: 'default' },
  },
)
```

### 5. Card mẫu

```tsx
<div className="bg-card rounded-2xl p-4 sm:p-6 border border-border card-hover"
     style={{ boxShadow: CARD_SHADOW }}>
  {/* … */}
</div>
```

### 6. Shell / kiến trúc UI

- **1 route** `app/page.tsx` gated bởi `useAuth()`: loader khi `!ready` → `LoginScreen` khi `!user` → console.
- Console = `AppSidebar` + `MainContent`; `MainContent` switch trên một **`Section` union** trong state (không dùng router).
- Module UI đặt ở `components/dashboard/content/<domain>/`.
- Dữ liệu chia sẻ đi qua store/service ở `lib/services/` (reactive store bằng `useSyncExternalStore`, mock/localStorage mặc định).

### 7. Setup nhanh

```bash
npx create-next-app@latest --ts --tailwind --app
# thêm: class-variance-authority clsx tailwind-merge @radix-ui/react-slot tw-animate-css
# fonts: DM Sans / Source Serif 4 / JetBrains Mono (next/font hoặc @import)
# lấy primitives cần dùng từ shadcn/ui, sửa token cho khớp globals.css trên
```

### Checklist mỗi component mới
- [ ] Chỉ dùng token, không hex.
- [ ] Card theo recipe + `CARD_SHADOW`.
- [ ] `type` không `interface`; union không `enum`.
- [ ] Data trong `useEffect`/store; guard `window`; `try/catch` `JSON.parse`.
- [ ] Có `focus-visible` ring; animation qua util có sẵn.
