import { BackupExtensionAbility } from '@kit.CoreFileKit';
// The Demo disables system backup/restore. Production hosts that enable backup
// must exclude every hiface-runtime directory in their merged backup profile.
export default class HiFaceBackupExtension extends BackupExtensionAbility {}
