<template>
  <div>
    <el-tabs v-model="activeTab">
      <el-tab-pane label="节点管理" name="nodes">
        <el-card>
          <template #header>节点列表</template>
          <el-table :data="nodes" stripe>
            <el-table-column prop="ip" label="IP" />
            <el-table-column prop="port" label="端口" width="80" />
            <el-table-column prop="hostname" label="主机名" />
            <el-table-column prop="region" label="区域" width="100" />
            <el-table-column prop="status" label="状态" width="100">
              <template #default="{ row }">
                <el-tag :type="row.status === 'online' ? 'success' : row.status === 'healthy' ? 'success' : 'danger'" size="small">{{ row.status }}</el-tag>
              </template>
            </el-table-column>
            <el-table-column prop="last_heartbeat" label="最后心跳" width="180" />
            <el-table-column label="操作" width="160">
              <template #default="{ row }">
                <el-button size="small" @click="setNodeStatus(row.id, 'offline')">下线</el-button>
                <el-button size="small" type="success" @click="setNodeStatus(row.id, 'online')">上线</el-button>
              </template>
            </el-table-column>
          </el-table>
        </el-card>
      </el-tab-pane>

      <el-tab-pane label="配置下发" name="config">
        <el-card>
          <template #header>
            <div style="display: flex; justify-content: space-between; align-items: center">
              <span>配置管理</span>
              <el-button type="primary" size="small" @click="showConfigDialog = true"><el-icon><Plus /></el-icon> 新增配置</el-button>
            </div>
          </template>
          <el-table :data="configs" stripe>
            <el-table-column prop="scope" label="范围" width="80" />
            <el-table-column prop="scope_key" label="作用域" />
            <el-table-column prop="key" label="键" />
            <el-table-column prop="value" label="值" />
            <el-table-column prop="version" label="版本" width="70" />
            <el-table-column prop="status" label="状态" width="90">
              <template #default="{ row }">
                <el-tag :type="row.status === 'applied' ? 'success' : row.status === 'pending' ? 'warning' : 'info'" size="small">{{ row.status }}</el-tag>
              </template>
            </el-table-column>
            <el-table-column label="操作" width="140">
              <template #default="{ row }">
                <el-button v-if="row.status === 'pending'" size="small" type="success" @click="applyConfig(row.id)">生效</el-button>
                <el-button v-if="row.status === 'applied'" size="small" type="warning" @click="rollbackConfig(row.id)">回滚</el-button>
              </template>
            </el-table-column>
          </el-table>
        </el-card>
      </el-tab-pane>

      <el-tab-pane label="审计日志" name="audit">
        <el-card>
          <template #header>审计日志</template>
          <el-table :data="auditLogs" stripe size="small">
            <el-table-column prop="action" label="操作" width="150" />
            <el-table-column prop="resource_type" label="资源类型" width="100" />
            <el-table-column prop="resource_id" label="资源ID" width="120" />
            <el-table-column prop="operator" label="操作人" width="100" />
            <el-table-column prop="created_at" label="时间" width="180" />
          </el-table>
        </el-card>
      </el-tab-pane>

      <el-tab-pane label="告警管理" name="alerts">
        <el-card>
          <template #header>告警规则</template>
          <el-table :data="alertRules" stripe>
            <el-table-column prop="name" label="规则名" />
            <el-table-column prop="condition" label="条件" width="150" />
            <el-table-column prop="threshold" label="阈值" width="80" />
            <el-table-column prop="severity" label="级别" width="90">
              <template #default="{ row }">
                <el-tag :type="row.severity === 'critical' ? 'danger' : row.severity === 'warning' ? 'warning' : 'info'" size="small">{{ row.severity }}</el-tag>
              </template>
            </el-table-column>
            <el-table-column prop="enabled" label="启用" width="70">
              <template #default="{ row }">
                <el-switch v-model="row.enabled" size="small" />
              </template>
            </el-table-column>
          </el-table>
        </el-card>
      </el-tab-pane>
    </el-tabs>

    <el-dialog v-model="showConfigDialog" title="新增配置" width="480px">
      <el-form :model="configForm" label-width="80px">
        <el-form-item label="范围">
          <el-select v-model="configForm.scope">
            <el-option label="全局" value="global" />
            <el-option label="服务级" value="service" />
            <el-option label="方法级" value="method" />
          </el-select>
        </el-form-item>
        <el-form-item label="作用域"><el-input v-model="configForm.scope_key" placeholder="服务名或 服务/方法" /></el-form-item>
        <el-form-item label="键"><el-input v-model="configForm.key" /></el-form-item>
        <el-form-item label="值"><el-input v-model="configForm.value" type="textarea" :rows="3" /></el-form-item>
      </el-form>
      <template #footer>
        <el-button @click="showConfigDialog = false">取消</el-button>
        <el-button type="primary" @click="addConfig">确定</el-button>
      </template>
    </el-dialog>
  </div>
</template>

<script setup lang="ts">
import { ref, onMounted } from 'vue'
import { Plus } from '@element-plus/icons-vue'
import { ElMessage } from 'element-plus'
import api from '../api/client'

const activeTab = ref('nodes')
const nodes = ref<any[]>([])
const configs = ref<any[]>([])
const auditLogs = ref<any[]>([])
const alertRules = ref<any[]>([])
const showConfigDialog = ref(false)
const configForm = ref({ scope: 'global', scope_key: '', key: '', value: '' })

async function fetchAll() {
  try { nodes.value = (await api.get('/api/v1/cluster/nodes')).data } catch {}
  try { configs.value = (await api.get('/api/v1/config', { params: { latest_only: true } })).data } catch {}
  try { auditLogs.value = (await api.get('/api/v1/audit/logs')).data } catch {}
  try { alertRules.value = (await api.get('/api/v1/alerts/rules')).data } catch {}
}

async function setNodeStatus(id: number, status: string) {
  await api.put(`/api/v1/cluster/nodes/${id}/status`, { status })
  ElMessage.success(`节点已设为 ${status}`)
  fetchAll()
}

async function addConfig() {
  await api.post('/api/v1/config', configForm.value)
  ElMessage.success('配置已创建'); showConfigDialog.value = false; fetchAll()
}

async function applyConfig(id: number) {
  await api.post(`/api/v1/config/${id}/apply`)
  ElMessage.success('已生效'); fetchAll()
}

async function rollbackConfig(id: number) {
  await api.post(`/api/v1/config/${id}/rollback`)
  ElMessage.success('已回滚'); fetchAll()
}

onMounted(fetchAll)
</script>
