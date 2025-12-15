import { defineConfig } from "vite";

export default defineConfig({
  server: {
    proxy: {
      "/api/": "http://billboard.local",
    },
  },
});
