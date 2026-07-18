<template>
  <div>
    <el-card>
      <template #header>
        <div style="display: flex; justify-content: space-between; align-items: center">
          <span>服务列表</span>
          <el-button type="primary" @click="showAdd = true"><el-icon><Plus /></el-icon> 注册服务</el-button>
        </div>
      </template>
      <el-table :data="services" stripe v-loading="loading">
        <el-table-column prop="name" label="服务名" />
        <el-table-column prop="version" label="版本" width="100" />
        <el-table-column prop="owner" label="负责人" width="120" />
        <el-table-column label="方法列表">
          <template #default="{ row }">
            <el-tag v-for="m in (row.methods || [])" :key="m" size="small" style="margin: 2px">{{ m }}</el-tag>
          </template>
        </el-table-column>
        <el-table-column prop="status" label="状态" width="90">
          <template #default="{ row }">
            <el-tag :type="row.status === 'active' ? 'success' : 'info'" size="small">{{ row.status }}</el-tag>
          </template>
        </el-table-column>
        <el-table-column label="操作" width="200">
          <template #default="{ row }">
            <el-button size="small" @click="viewInstances(row)">实例</el-button>
            <el-popconfirm title="确认删除?" @confirm="deleteService(row.name)">
              <template #reference><el-button size="small" type="danger">删除</el-button></template>
            </el-popconfirm>
          </template>
        </el-table-column>
      </el-table>
    </el-card>

    <el-dialog v-model="showAdd" title="注册新服务" width="480px">
      <el-form :model="form" label-width="80px">
        <el-form-item label="服务名"><el-input v-model="form.name" /></el-form-item>
        <el-form-item label="负责人"><el-input v-model="form.owner" /></el-form-item>
        <el-form-item label="版本"><el-input v-model="form.version" /></el-form-item>
        <el-form-item label="方法列表">
          <el-input v-model="methodsInput" placeholder="逗号分隔，如 Login,GetInfo" />
        </el-form-item>
      </el-form>
      <template #footer>
        <el-button @click="showAdd = false">取消</el-button>
        <el-button type="primary" @click="addService">确定</el-button>
      </template>
    </el-dialog>

    <el-drawer v-model="showInstances" :title="`${currentService} 实例列表`" size="400px">
      <el-table :data="instances" size="small">
        <el-table-column prop="node_ip" label="IP" />
        <el-table-column prop="node_port" label="端口" width="80" />
        <el-table-column prop="weight" label="权重" width="70" />
        <el-table-column prop="status" label="状态" width="80">
          <template #default="{ row }">
            <el-tag :type="row.status === 'online' ? 'success' : 'danger'" size="small">{{ row.status }}</el-tag>
          </template>
        </el-table-column>
      </el-table>
    </el-drawer>
  </div>
</template>

<script setup lang="ts">
import { ref, onMounted } from 'vue'
import { Plus } from '@element-plus/icons-vue'
import { ElMessage } from 'element-plus'
import api from '../api/client'

const services = ref<any[]>([])
const instances = ref<any[]>([])
const loading = ref(false)
const showAdd = ref(false)
const showInstances = ref(false)
const currentService = ref('')
const methodsInput = ref('')
const form = ref({ name: '', owner: '', version: '1.0.0' })

async function fetchServices() {
  loading.value = true
  try { services.value = (await api.get('/api/v1/cluster/services')).data } catch { /* offline */ }
  loading.value = false
}

async function addService() {
  const methods = methodsInput.value.split(',').map(s => s.trim()).filter(Boolean)
  try {
    await api.post('/api/v1/cluster/services', { ...form.value, methods })
    ElMessage.success('注册成功')
    showAdd.value = false
    fetchServices()
  } catch (e: any) { ElMessage.error(e.response?.data?.detail || '注册失败') }
}

async function deleteService(name: string) {
  await api.delete(`/api/v1/cluster/services/${name}`)
  ElMessage.success('已删除')
  fetchServices()
}

async function viewInstances(row: any) {
  currentService.value = row.name
  showInstances.value = true
  try { instances.value = (await api.get(`/api/v1/cluster/services/${row.name}/instances`)).data } catch { instances.value = [] }
}

onMounted(fetchServices)
</script>
