import type { SidebarConfig } from "@vuepress/theme-default";

export const zh: SidebarConfig = {
  "/zh/deploying": [
    {
      text: "部署指南",
      children: [
        "/zh/deploying/introduction.md",
        "/zh/deploying/quick-start.md",
        {
          text: "进阶部署",
          link: "/zh/deploying/deploy.md",
          children: [
            {
              text: "共享存储设备的准备",
              children: [
                "/zh/deploying/storage-aliyun-essd.md",
                "/zh/deploying/storage-curvebs.md",
                "/zh/deploying/storage-ceph.md",
                "/zh/deploying/storage-nbd.md",
              ],
            },
            {
              text: "文件系统的准备",
              children: [
                "/zh/deploying/fs-pfs.md",
                "/zh/deploying/fs-pfs-curve.md",
              ],
            },
            {
              text: "部署 PolarDB 数据库",
              children: [
                "/zh/deploying/db-localfs.md",
                "/zh/deploying/db-pfs.md",
                "/zh/deploying/db-pfs-curve.md",
              ],
            },
          ],
        },
        {
          text: "更多部署方式",
          children: [
            "/zh/deploying/deploy-stack.md",
            "/zh/deploying/deploy-official.md",
          ],
        },
      ],
    },
  ],
  "/zh/operation/": [
    {
      text: "使用与运维",
      children: [
        {
          text: "日常运维",
          children: [
            "/zh/operation/backup-and-restore.md",
            "/zh/operation/grow-storage.md",
            "/zh/operation/scale-out.md",
            "/zh/operation/ro-online-promote.md",
          ],
        },
        {
          text: "问题诊断",
          children: ["/zh/operation/cpu-usage-high.md"],
        },
        {
          text: "性能测试",
          children: [
            "/zh/operation/tpcc-test.md",
            "/zh/operation/tpch-on-px.md",
          ],
        },
      ],
    },
  ],
  "/zh/features": [
    {
      text: "内核特性",
      link: "/zh/features/v11/",
      children: [
        {
          text: "PolarDB for PostgreSQL 11",
          link: "/zh/features/v11/",
          children: [
            {
              text: "高性能",
              link: "/zh/features/v11/performance/",
            },
            {
              text: "高可用",
              link: "/zh/features/v11/availability/",
            },
            {
              text: "安全",
              link: "/zh/features/v11/security/",
            },
            {
              text: "HTAP",
              link: "/zh/features/v11/htap/",
            },
          ],
        },
      ],
    },
  ],
  "/zh/theory/": [
    {
      text: "原理解读",
      children: [
        {
          text: "PolarDB for PostgreSQL",
          children: [
            "/zh/theory/arch-overview.md",
            "/zh/theory/arch-htap.md",
            "/zh/theory/buffer-management.md",
            "/zh/theory/ddl-synchronization.md",
            "/zh/theory/logindex.md",
          ],
        },
        {
          text: "PostgreSQL",
          children: [
            "/zh/theory/analyze.md",
            "/zh/theory/polar-sequence-tech.md",
          ],
        },
      ],
    },
  ],
  "/zh/development/": [
    {
      text: "上手开发",
      children: [
        "/zh/development/dev-on-docker.md",
        "/zh/development/customize-dev-env.md",
      ],
    },
  ],
  "/zh/contributing": [
    {
      text: "参与社区",
      children: [
        "/zh/contributing/contributing-polardb-kernel.md",
        "/zh/contributing/contributing-polardb-docs.md",
        "/zh/contributing/coding-style.md",
        "/zh/contributing/code-of-conduct.md",
      ],
    },
  ],
};
